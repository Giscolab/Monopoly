#include "ModernIBarSkin.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace
{
    using namespace monopoly;
    void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
    auto bitmap(data::DataId id,unsigned w,unsigned h)
    {
        auto asset=std::make_shared<data::BitmapRuntimeAsset>();
        asset->dataId=id;asset->sourceType=data::LegacyDataType::Uap;
        asset->image={w,h,std::vector<std::uint8_t>(std::size_t(w)*h*4,255)};
        return asset;
    }
    void testPortfolioMiniatures()
    {
        bool context=true,failFont=false;
        std::vector<std::string> labels;
        unsigned requested=99;
        const auto raster=[&](std::string_view text)->std::expected<data::LegacyBitmapRGBA8,std::string> {
            labels.emplace_back(text);
            if(failFont)return std::unexpected("font unavailable");
            return data::LegacyBitmapRGBA8{unsigned(text.size()*4+1),10,
                std::vector<std::uint8_t>((text.size()*4+1)*10*4,127)};
        };
        ibar::ModernIBarSkin skin(data::LanguageId::EnglishUs,raster);
        skin.configurePresentationContext([&]{return context;});
        skin.configurePropertyDescriptors([&](unsigned index)->std::optional<ibar::ModernIBarSkin::PropertyDescriptor> {
            requested=index;
            return ibar::ModernIBarSkin::PropertyDescriptor{index==26?"Park Place":"Reading Railroad",
                std::uint8_t(index==26?7:8),"$350"};
        },raster);
        const auto normal=bitmap(0x00030600,36,42),mortgage=bitmap(0x000305E4,36,42);
        for(std::size_t index=3;index<normal->image.pixels.size();index+=4)
            normal->image.pixels[index]=mortgage->image.pixels[index]=std::uint8_t(index/4%256);
        const auto modern=skin.substitute(normal->dataId,normal);
        require(modern!=normal && requested==26 && modern->image.width==108 && modern->image.height==126 &&
            !modern->presentationRect,"miniature retains exact36x42 footprint at3x with no placement override");
        require(std::find(labels.begin(),labels.end(),"Park Place")!=labels.end() &&
            std::find(labels.begin(),labels.end(),"$350")!=labels.end(),"authentic descriptor name and purchase text reach rasterizer");
        const auto header=(std::size_t(10)*108+30)*4;
        require(modern->image.pixels[header]==45 && modern->image.pixels[header+1]==65 && modern->image.pixels[header+2]==144,
            "Park Place preserves canonical darkblue group header");
        const auto mortgaged=skin.substitute(mortgage->dataId,mortgage);
        require(mortgaged!=mortgage && requested==26 && mortgaged->image.pixels!=modern->image.pixels,
            "mortgage miniature retains same property identity and visibly distinct state");
        for(const auto& pair:{std::pair{normal,modern},std::pair{mortgage,mortgaged}})
            for(unsigned y=0;y<126;++y)for(unsigned x=0;x<108;++x)
                require(pair.second->image.pixels[(std::size_t(y)*108+x)*4+3]==
                    pair.first->image.pixels[(std::size_t(y/3)*36+x/3)*4+3],"every original miniature alpha byte survives supersampling and captions");
        require(skin.substitute(normal->dataId,normal)==modern,"qualified miniature uses bounded existing derivative cache");
        context=false;require(skin.substitute(normal->dataId,normal)==normal,"context loss returns retail before cache hit");context=true;
        require(skin.substitute(normal->dataId,normal,false)==normal,"secondary leaves remain untouched");
        const auto wrongLeaf=bitmap(normal->dataId+1,36,42);
        require(skin.substitute(normal->dataId,wrongLeaf)==wrongLeaf,
            "different root and leaf cannot reuse qualified derivative");
        auto changedAlpha=std::make_shared<data::BitmapRuntimeAsset>(*normal);
        const auto hash=[](const auto& pixels){std::uint64_t value=14695981039346656037ULL;
            for(const auto byte:pixels){value^=byte;value*=1099511628211ULL;}return value;};
        const auto retiredHash=hash(modern->image.pixels);
        changedAlpha->image.pixels[3]=255;
        const auto refreshed=skin.substitute(normal->dataId,changedAlpha);
        require(refreshed!=modern && refreshed->image.pixels[3]==255 && modern->image.pixels[3]==0,
            "new source alpha replaces cached derivative without mutating retired artwork");
        require(hash(modern->image.pixels)==retiredHash && skin.substitute(normal->dataId,normal)==modern,
            "same-ID immutable assets retain separate cached artwork and unchanged old pixel hash");
        const auto rasterCalls=labels.size();
        require(skin.substitute(normal->dataId,changedAlpha)==refreshed && labels.size()==rasterCalls,
            "same immutable source returns directly without rerasterizing captions");
        std::weak_ptr<const data::BitmapRuntimeAsset> retainedSource=changedAlpha;
        changedAlpha.reset();
        require(!retainedSource.expired(),"miniature cache retains source identity against allocator pointer reuse");
        for(const auto pair:{std::pair{0x000305E6U,0U},std::pair{0x00030601U,27U},
            std::pair{0x000305CAU,0U},std::pair{0x000305E5U,27U}})
        {
            const auto edge=bitmap(pair.first,36,42);
            require(skin.substitute(pair.first,edge)!=edge && requested==pair.second,
                "both exact miniature ranges retain first and final property indices");
        }
        auto wrong=bitmap(normal->dataId,35,42);require(skin.substitute(normal->dataId,wrong)==wrong,"unmeasured raster size rejected");
        wrong=bitmap(normal->dataId,36,42);wrong->image.pixels.pop_back();
        require(skin.substitute(normal->dataId,wrong)==wrong,"malformed raster rejected");
        wrong=bitmap(normal->dataId,36,42);wrong->sourceType=data::LegacyDataType::Bitmap;
        require(skin.substitute(normal->dataId,wrong)==wrong,"wrong resource type rejected");
        for(const auto root:{0x000305C9U,0x00030602U,0x00050600U})
            require(!skin.supports(root),"neighboring tags and wrong bank remain unqualified");
        const auto railroad=bitmap(0x000305E8,36,42);
        require(skin.substitute(railroad->dataId,railroad)!=railroad && requested==2,
            "miniature index remains board-order property identity rather than player ownership order");
        failFont=true;
        const auto uncached=bitmap(0x000305E9,36,42);
        require(skin.substitute(uncached->dataId,uncached)==uncached,"font failure publishes complete retail fallback");
        failFont=false;
        skin.configurePropertyDescriptors([](unsigned)->std::optional<ibar::ModernIBarSkin::PropertyDescriptor>{return {};},raster);
        require(retainedSource.expired(),"clearing derivative cache releases retained miniature sources");
        require(skin.substitute(normal->dataId,normal)==normal,"missing canonical property descriptor retains retail");
        ibar::ModernIBarSkin french(data::LanguageId::French,raster);
        require(!french.supports(normal->dataId),"miniature qualification remains USA English only");
    }
    void testMeasuredDeedArtwork()
    {
        using Kind=ibar::ModernIBarSkin::DeedArtwork;
        struct Case {data::DataId root;Kind kind;std::array<unsigned,4> crop;};
        const std::array cases{Case{0x00050CD2,Kind::Railroad,{68,18,125,61}},
            Case{0x00050CDA,Kind::Railroad,{68,18,125,61}},Case{0x00050CE1,Kind::Railroad,{68,18,125,61}},
            Case{0x00050CE9,Kind::Railroad,{68,18,125,61}},Case{0x00050CD7,Kind::Electric,{74,16,114,63}},
            Case{0x00050CE4,Kind::Water,{73,18,127,63}}};
        ibar::ModernIBarSkin skin(data::LanguageId::EnglishUs,{});
        bool usa=true;skin.configurePresentationContext([&]{return usa;});
        for(const auto& c:cases)
        {
            ibar::ModernIBarSkin::DeedDescriptor descriptor;
            descriptor.artwork=c.kind;
            descriptor.text={{"EXACT TITLE",72,12,1,0,12,0,false,false,false},
                {"EXACT RENT $25",92,14,1,0,12,0,false,false,false}};
            std::vector<std::tuple<std::string,int,bool>> requests;
            skin.configureDeedDescriptors({{c.root,descriptor}},
                [&](std::string_view text,int size,bool bold,bool)->std::expected<data::LegacyBitmapRGBA8,std::string>
                {
                    requests.emplace_back(text,size,bold);
                    return data::LegacyBitmapRGBA8{24,12,std::vector<std::uint8_t>(24*12*4,0)};
                });
            auto source=bitmap(c.root,199,227);
            const unsigned x=c.crop[0]+1,y=c.crop[1]+1;
            const auto native=(std::size_t(y)*199+x)*4;
            source->image.pixels[native]=source->image.pixels[native+1]=source->image.pixels[native+2]=128;
            const auto modern=skin.substitute(c.root,source);
            require(modern!=source && modern->image.width==597 && modern->image.height==681 &&
                modern->dataId==source->dataId,"six exact measured USA front owners preserve199x227 authored extents at3x");
            for(unsigned dy=0;dy<3;++dy)for(unsigned dx=0;dx<3;++dx)
            {
                const auto pixel=(std::size_t(y*3+dy)*597+x*3+dx)*4;
                require(modern->image.pixels[pixel]==119 && modern->image.pixels[pixel+1]==116 &&
                    modern->image.pixels[pixel+2]==108 && modern->image.pixels[pixel+3]==255,
                    "original grayscale128 retains exact127 ink coverage composited onto cream without white patch");
            }
            const auto white=(std::size_t(y*3)*597+(x+1)*3)*4;
            require(modern->image.pixels[white]==237 && modern->image.pixels[white+1]==232 &&
                modern->image.pixels[white+2]==215,"original white artwork background becomes unchanged cream");
            require(std::find(requests.begin(),requests.end(),std::tuple{std::string{"EXACT TITLE"},48,true})!=requests.end() &&
                std::find(requests.begin(),requests.end(),std::tuple{std::string{"EXACT RENT $25"},36,false})!=requests.end(),
                "only title gets bold16px; exact canonical rent text and font remain unchanged");
            usa=false;require(skin.substitute(c.root,source)==source,"artwork context checked before cached principal");usa=true;
            auto bad=std::make_shared<data::BitmapRuntimeAsset>(*source);bad->image.pixels[native+3]=128;
            require(skin.substitute(c.root,bad)==bad,"partial-alpha unqualified native art falls back before cache");
            bad->image.pixels[native+3]=255;bad->image.pixels[native+1]=127;
            require(skin.substitute(c.root,bad)==bad,"nonmonochrome unqualified artwork retains entire original deed");
            auto wrong=bitmap(c.root+1,199,227);
            require(skin.substitute(c.root,wrong)==wrong,"artwork requires exact original payload identity");
            bad=bitmap(c.root,199,227);
            require(skin.substitute(c.root,bad)==bad,"empty all-white artwork fails whole owner before cache");
            descriptor.artwork=c.kind==Kind::Railroad?Kind::Water:Kind::Railroad;
            skin.configureDeedDescriptors({{c.root,descriptor}},[](std::string_view,int,bool,bool)->std::expected<data::LegacyBitmapRGBA8,std::string>
                {return std::unexpected("must not reach font");});
            require(skin.substitute(c.root,source)==source,"mismatched measured kind/root preserves original owner");
        }
        ibar::ModernIBarSkin::DeedDescriptor invalid;
        invalid.artwork=Kind::Water;invalid.text.push_back({"Unknown",72,12,1,0,12});
        skin.configureDeedDescriptors({{0x00050CE3,invalid}},
            [](std::string_view,int,bool,bool)->std::expected<data::LegacyBitmapRGBA8,std::string>
            {return data::LegacyBitmapRGBA8{1,1,{0,0,0,0}};});
        const auto ventnor=bitmap(0x00050CE3,199,227);
        require(skin.substitute(0x00050CE3,ventnor)==ventnor,
            "Ventnor CE3 never accepts Water Works CE4 artwork");
    }
    void testScoreTokenImages()
    {
        const auto raster=[](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>
        {return data::LegacyBitmapRGBA8{1,1,{255,255,255,255}};};
        ibar::ModernIBarSkin skin(data::LanguageId::EnglishUs,raster);
        bool usa=true;skin.configurePresentationContext([&]{return usa;});
        auto photo=std::make_shared<data::LegacyBitmapRGBA8>();
        *photo={768,640,std::vector<std::uint8_t>(768*640*4,0)};
        for(unsigned y=200;y<300;++y)for(unsigned x=100;x<300;++x)
        {const auto i=(std::size_t(y)*768+x)*4;photo->pixels[i]=20;photo->pixels[i+1]=40;photo->pixels[i+2]=60;photo->pixels[i+3]=128;}
        unsigned calls=0,last=99;
        skin.configureTokenImages([&](std::uint8_t token){++calls;last=token;return photo;});
        constexpr std::array<std::array<unsigned,2>,11> sizes{{{53,29},{52,22},{39,29},{39,26},{39,26},{22,29},
            {54,32},{47,28},{22,29},{49,20},{24,31}}};
        for(unsigned token=0;token<11;++token)
        {
            const auto source=bitmap(0x000201C0+token,sizes[token][0],sizes[token][1]);
            const auto result=skin.substitute(source->dataId,source);
            require(result!=source && last==token && result->dataId==source->dataId && result->preferLinearFiltering &&
                result->image.width==sizes[token][0]*3 && result->image.height==sizes[token][1]*3 &&
                result->presentationRect && *result->presentationRect==std::array<float,4>{0,0,float(sizes[token][0]),float(sizes[token][1])},
                "all eleven exact score tokens retain authored intrinsic footprints and placement rectangle");
            unsigned l=result->image.width,t=result->image.height,r=0,b=0;
            for(unsigned y=0;y<result->image.height;++y)for(unsigned x=0;x<result->image.width;++x)
            {
                const auto i=(std::size_t(y)*result->image.width+x)*4;
                if(result->image.pixels[i+3])
                {l=std::min(l,x);t=std::min(t,y);r=std::max(r,x+1);b=std::max(b,y+1);
                    require(result->image.pixels[i]==20 && result->image.pixels[i+1]==40 && result->image.pixels[i+2]==60 &&
                        result->image.pixels[i+3]==128,"token compositing retains actual RGB and partial alpha");}
            }
            require(l>=3 && t>=3 && r+3<=result->image.width && b+3<=result->image.height &&
                std::abs(int(r-l)-2*int(b-t))<=2,"alpha crop contains original2:1 object with one native pixel transparent padding");
            const auto before=calls;
            require(skin.substitute(source->dataId,source)==result && calls==before,"token cache avoids repeated image crop");
            usa=false;
            require(skin.substitute(source->dataId,source)==source,"USA eligibility is checked before cached token hit");
            usa=true;
            require(skin.substitute(source->dataId,source,false)==source,"secondary score token leaf stays original");
            const auto wrong=bitmap(source->dataId,sizes[token][0]+1,sizes[token][1]);
            require(skin.substitute(source->dataId,wrong)==wrong,"token wrong source extent falls back before cache hit");
            const auto wrongId=bitmap(source->dataId+1,sizes[token][0],sizes[token][1]);
            require(skin.substitute(source->dataId,wrongId)==wrongId,"token content ID must match exact score owner");
        }
        const auto source=bitmap(0x000201C0,53,29);
        skin.configureTokenImages([](std::uint8_t)->std::shared_ptr<const data::LegacyBitmapRGBA8>{return {};});
        require(skin.substitute(source->dataId,source)==source,"provider change retires cached token; missing image keeps fallback");
        for(unsigned mode=0;mode<4;++mode)
        {
            auto bad=std::make_shared<data::LegacyBitmapRGBA8>(*photo);
            if(mode==0)bad->pixels.pop_back();
            if(mode==1)bad->width=767;
            if(mode==2)std::fill(bad->pixels.begin(),bad->pixels.end(),0);
            if(mode==3)for(std::size_t i=3;i<bad->pixels.size();i+=4)bad->pixels[i]=255;
            skin.configureTokenImages([bad](std::uint8_t){return bad;});
            require(skin.substitute(source->dataId,source)==source,"malformed/wrongextent/empty/opaque PNG retains original token");
        }
        require(!skin.supports(0x000201BF) && !skin.supports(0x000301C0),"jail bars and wrong-group token owner remain unchanged");
        ibar::ModernIBarSkin unqualified(data::LanguageId::EnglishUs,raster);
        unqualified.configureTokenImages([photo](std::uint8_t){return photo;});
        require(unqualified.substitute(source->dataId,source)==source,
            "token images require the explicit current USA presentation predicate");
        ibar::ModernIBarSkin uk(data::LanguageId::EnglishUk,raster);
        uk.configurePresentationContext([]{return true;});uk.configureTokenImages([photo](std::uint8_t){return photo;});
        require(uk.substitute(source->dataId,source)==source,"score token provider is strictly EnglishUS");
    }

    void testPurchaseDeedPlacement()
    {
        const auto raster=[](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>
        {return data::LegacyBitmapRGBA8{24,12,std::vector<std::uint8_t>(24*12*4,255)};};
        ibar::ModernIBarSkin skin(data::LanguageId::EnglishUs,raster);
        ibar::ModernIBarSkin::DeedDescriptor descriptor;
        descriptor.text.push_back({"Park Place",10,20,1,0,12,0xFFFFFF,true,false,true});
        skin.configureDeedDescriptors({{0x00050CEA,descriptor}},
            [&](std::string_view value,int,bool,bool){return raster(value);});
        bool usa=true,trade=false;unsigned calls=0;
        skin.configurePresentationContext([&]{return usa;});
        skin.configureDeedPlacementProvider([&](data::DataId root,std::uint16_t priority,const sequence::Matrix2D& world)
            ->std::optional<std::array<float,4>>
        {
            ++calls;require(root==0x00050CEA && priority==1002,"provider sees actual purchase owner and priority");
            if(!trade)return {};
            return std::array<float,4>{601.37665F-world.values[6],-world.values[7],
                798.62335F-world.values[6],225-world.values[7]};
        });
        const auto source=bitmap(0x00050CEA,199,227);
        const auto world=sequence::translate2D(20,110);
        const auto authored=skin.substitute(source->dataId,source,true,world,1002);
        require(authored!=source && !authored->presentationRect,"purchase deed keeps original placement while Trade closed");
        trade=true;
        const auto moved=skin.substitute(source->dataId,source,true,world,1002);
        require(moved!=authored && moved->dataId==source->dataId && moved->presentationRect &&
            moved->image.width==597 && moved->image.height==681,
            "Trade purchase rectangle gets distinct cache entry without changing deed raster or identity");
        const auto& rect=*moved->presentationRect;
        require(std::abs(rect[0]+20-601.37665F)<.001F && std::abs(rect[1]+110)<.001F &&
            std::abs(rect[2]+20-798.62335F)<.001F && std::abs(rect[3]+110-225)<.001F,
            "local rectangle maps exact existing20,110 purchase translation into requested Trade display area");
        const auto before=calls;
        const auto hover=skin.substitute(source->dataId,source,true,world,1003);
        require(hover==authored && !hover->presentationRect && calls==before,
            "hover priority1003 remains independently at authored geometry");
        require(!skin.substitute(source->dataId,source,false,world,1002)->presentationRect && calls==before,
            "secondary deed leaves never invoke placement provider");
        const auto otherWorld=sequence::translate2D(30,120);
        const auto other=skin.substitute(source->dataId,source,true,otherWorld,1002);
        require(other!=moved && other->presentationRect && (*other->presentationRect)[0]!=rect[0],
            "local rectangles from different transforms remain distinct cache identities");
        usa=false;const auto guarded=calls;
        require(skin.substitute(source->dataId,source,true,world,1002)==source && calls==guarded,
            "current presentation context is checked before cached deed placement or callback");
        usa=true;trade=false;
        require(skin.substitute(source->dataId,source,true,world,1002)==authored,
            "closing Trade recovers existing authored-placement cache entry");
        const auto wrong=bitmap(source->dataId,198,227);
        require(skin.substitute(source->dataId,wrong,true,world,1002)==wrong,"unqualified deed size cannot receive rectangle");
        for(const auto invalid:{std::array<float,4>{0,0,0,10},std::array<float,4>{0,0,801,10},
            std::array<float,4>{0,0,10,601},std::array<float,4>{0,0,10,std::numeric_limits<float>::infinity()}})
        {
            skin.configureDeedPlacementProvider([invalid](data::DataId,std::uint16_t,const sequence::Matrix2D&)
                ->std::optional<std::array<float,4>>{return invalid;});
            require(skin.substitute(source->dataId,source,true,world,1002)==source,
                "invalid nonfinite or oversized placement retains retail fallback");
        }
    }

    void testMeasuredRetailTrade()
    {
        std::string caption;
        ibar::ModernIBarSkin skin(data::LanguageId::EnglishUs,
            [&](std::string_view text)->std::expected<data::LegacyBitmapRGBA8,std::string>
            {caption=text;return data::LegacyBitmapRGBA8{30,10,std::vector<std::uint8_t>(30*10*4,255)};});
        skin.configureLayoutProvider([]{return ibar::layout::ActionButtonLayout::General;});
        bool usa=true;skin.configurePresentationContext([&]{return usa;});
        // Measured immutable retail dat_lm01 UAPs and exact CNK bitmap rectangle.
        const auto raster=sequence::translate2D(70,454);
        for(const auto base:{0x000500D6U,0x00050152U})
        {
            const auto asset=bitmap(base==0x000500D6?0x0005140E:0x00051628,60,32);
            for(unsigned state=0;state<3;++state)
            {
                const auto replacement=skin.substitute(base+state,asset,true,raster);
                require(replacement!=asset && caption=="Trade" && replacement->image.width==60 &&
                    replacement->image.height==32 && replacement->dataId==asset->dataId,
                    "actual settled trade/grey states modernize within original60x32 footprint");
                require(replacement==skin.substitute(base+state,asset,true,raster),"settled geometry reuses immutable cache");
                for(unsigned y=0;y<32;++y)for(unsigned x=0;x<60;++x)
                    require(!replacement->image.pixels[(std::size_t(y)*60+x)*4+3] ||
                        (x+70>=70 && x+70<132 && y+454>=455 && y+454<483),
                        "modern trade never paints outside unchanged62x28 interactive rectangle");
                usa=false;
                require(skin.substitute(base+state,asset,true,raster)==asset,
                    "USA context is checked before a cached settled Trade hit");
                usa=true;
            }
            auto shifted=raster;shifted.values[6]=40;
            require(skin.substitute(base+3,asset,true,shifted)==asset,
                "pressed moving CNK retains exact retail transition");
            auto shiftedIdle=raster;shiftedIdle.values[6]=69;
            require(skin.substitute(base+1,asset,true,shiftedIdle)==asset,
                "unmeasured shifted idle geometry is not relaxed");
            const auto wrong=bitmap(asset->dataId+1,60,32);
            require(skin.substitute(base+1,wrong,true,raster)==wrong,
                "unqualified bitmap payload cannot use Trade exception");
        }
        const auto mortgage=bitmap(0x00051357,120,85);
        require(skin.substitute(0x000500BB,mortgage,true,sequence::translate2D(459,453))!=mortgage && caption=="Mortgage",
            "actual mortgage idle already qualifies normal whole-band containment unchanged");
    }
}
int main()
{
    try{testPortfolioMiniatures();testMeasuredDeedArtwork();testMeasuredRetailTrade();testScoreTokenImages();testPurchaseDeedPlacement();std::cout<<"[PASS] measured Trade footprint and context fallback\n";return 0;}
    catch(const std::exception& e){std::cerr<<"[FAIL] "<<e.what()<<'\n';return 1;}
}
