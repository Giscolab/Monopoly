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
    try{testMeasuredRetailTrade();testScoreTokenImages();testPurchaseDeedPlacement();std::cout<<"[PASS] measured Trade footprint and context fallback\n";return 0;}
    catch(const std::exception& e){std::cerr<<"[FAIL] "<<e.what()<<'\n';return 1;}
}
