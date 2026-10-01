#include "ModernIBarSkin.hpp"
#include "ResourcePaths.hpp"
#include "ResourceRuntime.hpp"
#include "SequenceBitmapRenderData.hpp"
#include "SequenceWorld2DSlot.hpp"
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
    void testMeasuredStCharlesIdleCard()
    {
        const auto raster=[](std::string_view text)->std::expected<data::LegacyBitmapRGBA8,std::string>
        { return data::LegacyBitmapRGBA8{unsigned(text.size()*4+1),12,
            std::vector<std::uint8_t>((text.size()*4+1)*12*4,255)}; };
        ibar::ModernIBarSkin skin(data::LanguageId::EnglishUs,raster);
        bool context=true;
        skin.configurePresentationContext([&]{return context;});
        skin.configureDrawCardDescriptors({{0x00050029,{"Chance",
            "Advance to St. Charles Place. If you pass GO, collect $200.",400,239}}},
            [&](std::string_view text,int,bool,bool){return raster(text);});
        const auto original=bitmap(0x00050983,400,239);
        const auto result=skin.substitute(0x00050029,original);
        require(result!=original && result->image.width==1200 && result->image.height==717 &&
            result->dataId==original->dataId && result->preferLinearFiltering,
            "Production St. Charles idle400x239 face retains exact authored footprint");
        const auto wrong=bitmap(0x00050983,400,240);
        require(skin.substitute(0x00050029,wrong)==wrong,
            "St. Charles qualification rejects the assumed400x240 footprint");
        context=false;
        require(skin.substitute(0x00050029,original)==original,
            "Presentation context is checked before returning cached239px card");
        context=true;
        require(skin.substitute(0x00050019,bitmap(0x00050815,400,240))->sourceType==data::LegacyDataType::Uap,
            "Native FaceIn remains unqualified; idle dimension correction does not modernize animation");
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
    void testMeasuredNavigationAA()
    {
        using Slot=ibar::layout::ActionButtonSlot;
        struct Frame {unsigned first,last,leaf,width,height,x,y;Slot slot;const char* label;};
        // Actual immutable DAT settled states. Large Board/Status artwork retains
        // only its existing interactive band; moving leaves still use native guards.
        constexpr std::array<Frame,8> frames{{
            {0xBF,0xC0,0x1367,69,93,731,452,Slot::Status,"Board"},
            {0x13A,0x13C,0x159E,60,32,730,454,Slot::Status,"Board"},
            {0xC2,0xC4,0x136F,61,32,9,454,Slot::Options,"Options"},
            {0x13E,0x140,0x15A2,61,32,9,454,Slot::Options,"Options"},
            {0xD3,0xD4,0x1406,90,93,710,452,Slot::Status,"Status"},
            {0x14E,0x150,0x1620,60,32,730,454,Slot::Status,"Status"},
            {0xD6,0xD8,0x140E,60,32,70,454,Slot::Trade,"Trade"},
            {0x152,0x154,0x1628,60,32,70,454,Slot::Trade,"Trade"}
        }};
        std::string caption;unsigned calls=0;bool context=true;
        ibar::ModernIBarSkin skin(data::LanguageId::EnglishUs,
            [](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>
            {return std::unexpected("navigation must use optional high-resolution text");});
        skin.configureLayoutProvider([]{return ibar::layout::ActionButtonLayout::General;});
        skin.configurePresentationContext([&]{return context;});
        skin.configureActionText([&](std::string_view text)->std::expected<data::LegacyBitmapRGBA8,std::string>
        {caption=text;++calls;return data::LegacyBitmapRGBA8{unsigned(text.size()*18),36,
            std::vector<std::uint8_t>(text.size()*18*36*4,128)};});
        for(const auto& frame:frames)
        {
            const auto source=bitmap(0x50000+frame.leaf,frame.width,frame.height);
            for(std::size_t i=3;i<source->image.pixels.size();i+=4)source->image.pixels[i]=std::uint8_t((i/4*37)%256);
            const auto before=source->image.pixels;const auto raster=sequence::translate2D(float(frame.x),float(frame.y));
            const auto hit=ibar::layout::actionButtonRect(frame.slot);
            for(unsigned tag=frame.first;tag<=frame.last;++tag)
            {
                const auto result=skin.substitute(0x50000+tag,source,true,raster);
                require(result!=source && caption==frame.label && result->image.width==frame.width*3 &&
                    result->image.height==frame.height*3 && result->dataId==source->dataId &&
                    result->preferLinearFiltering && !result->presentationRect && source->image.pixels==before,
                    "All22 exact settled human/grey navigation states use high-resolution authentic captions at native footprint");
                const auto& image=result->image;
                for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x)
                {
                    const float wx=(x+.5F)/3+frame.x,wy=(y+.5F)/3+frame.y;
                    const bool inside=wx>=hit.left&&wx<hit.right&&wy>=hit.top&&wy<hit.bottom;
                    require(image.pixels[(std::size_t(y)*image.width+x)*4+3]==
                        (inside?source->image.pixels[(std::size_t(y/3)*frame.width+x/3)*4+3]:0),
                        "Navigation retains exact source alpha inside unchanged hit band and hides old glow outside it");
                }
                const unsigned cachedCalls=calls;
                require(skin.substitute(0x50000+tag,source,true,raster)==result && calls==cachedCalls,
                    "Source-aware navigation AA derivative is cached");
                context=false;require(skin.substitute(0x50000+tag,source,true,raster)==source,
                    "Unqualified presentation context restores original navigation pointer");context=true;
                auto moved=raster;moved.values[6]+=float(frame.width*2);
                require(skin.substitute(0x50000+tag,source,true,moved)==source,
                    "Moving navigation raster that cannot carry its interactive band retains authored pixels");
            }
        }
    }

    void inspectActionButtons(const std::filesystem::path& root)
    {
        const auto paths=data::ResourcePaths::create(std::array{root});data::ResourceRuntime resources;
        require(paths && resources.initialize(*paths).has_value(),"real USA DAT action-button resources open");
        const auto snapshot=resources.snapshot();
        for(const unsigned index:{0U,1U,3U,12U,13U,14U,17U,18U,19U})for(const unsigned base:{0x008AU,0x0106U})for(unsigned state=0;state<4;++state)
        {
            const auto id=data::packDataId(data::LegacyGroupId::LanguageGraphics,data::DataTag(base+index*4+state));
            auto program=sequence::SequenceProgram::load(snapshot,id,0);require(program.has_value(),"actual action CNK decodes");
            sequence::SequenceRuntime runtime;require(runtime.start(*program,999).has_value(),"actual action CNK starts");
            engine::SequenceWorld2DSlot slot;data::BitmapRuntimeCache cache;
            for(int tick=0;tick<=12;++tick)
            {
                require(runtime.update(tick).has_value(),"actual action clock advances");
                if(tick!=0 && tick!=4 && tick!=12)continue;
                auto items=sequence::collectSequenceBitmapRenderData(runtime,snapshot);require(items && slot.sync(*items,cache),"actual action leaves decode into production slot");
                for(const auto& item:*items)
                {
                    const auto* object=slot.find(item.node);
                    std::cout<<"action_leaf root="<<std::hex<<id<<" payload="<<item.contentsDataId<<std::dec
                        <<" tick="<<tick<<" size="<<object->asset->image.width<<'x'<<object->asset->image.height
                        <<" origin="<<item.metadata.originX<<','<<item.metadata.originY<<" matrix=";
                    for(const auto value:object->worldTransform.values)std::cout<<value<<',';
                    std::cout<<'\n';
                }
            }
        }
    }
    void testActualActionButtons(const std::filesystem::path& root)
    {
        const auto paths=data::ResourcePaths::create(std::array{root});data::ResourceRuntime resources;
        require(paths && resources.initialize(*paths).has_value(),"actual action button DAT opens");
        const auto snapshot=resources.snapshot();unsigned idleProofs=0,pressedProofs=0;
        for(const unsigned index:{0U,1U,3U,12U,13U,14U,17U,18U,19U})for(const unsigned base:{0x008AU,0x0106U})for(unsigned state=0;state<4;++state)
        {
            const auto id=data::packDataId(data::LegacyGroupId::LanguageGraphics,data::DataTag(base+index*4+state));
            auto program=sequence::SequenceProgram::load(snapshot,id,0);require(program.has_value(),"real action CNK loads");
            sequence::SequenceRuntime runtime;require(runtime.start(*program,999).has_value(),"real action CNK starts");
            std::string caption;unsigned glyphCalls=0;bool context=true;
            auto skin=std::make_shared<ibar::ModernIBarSkin>(data::LanguageId::EnglishUs,
                [](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>{return std::unexpected("native raster must not be used");});
            skin->configurePresentationContext([&]{return context;});
            skin->configureActionText([&](std::string_view text)->std::expected<data::LegacyBitmapRGBA8,std::string>
            {caption=text;++glyphCalls;return data::LegacyBitmapRGBA8{96,30,std::vector<std::uint8_t>(96*30*4,128)};});
            const auto layout=(index==0||index==1)?ibar::layout::ActionButtonLayout::BuyAuction:ibar::layout::ActionButtonLayout::General;
            skin->configureLayoutProvider([=]{return layout;});
            engine::SequenceWorld2DSlot native,modern;data::BitmapRuntimeCache cache;
            modern.configureModernIBarSkin(skin);
            for(int tick=0;tick<=12;++tick)
            {
                require(runtime.update(tick).has_value(),"actual action state advances");
                if(tick!=0 && tick!=4 && tick!=12)continue;
                auto items=sequence::collectSequenceBitmapRenderData(runtime,snapshot);
                require(items && native.sync(*items,cache) && modern.sync(*items,cache),"actual metadata and sourcealpha reach skin seam");
                bool replaced=false;
                for(const auto node:native.order())
                {
                    const auto* a=native.find(node);const auto* m=modern.find(node);
                    require(m && m->clock==a->clock && m->priority==a->priority && m->contentsDataId==a->contentsDataId,"actual CNK state clock/priority/content IDs unchanged");
                    if(m->asset==a->asset)continue;
                    replaced=true;
                    require(m->asset->image.width==a->asset->image.width*3 && m->asset->image.height==a->asset->image.height*3 &&
                        !m->asset->presentationRect,"actual action bitmap has3x pixels and original intrinsic rectangle");
                    require(engine::SequenceWorld2DSlot::transformPoint(m->worldTransform,m->asset->image.width,m->asset->image.height)==
                        engine::SequenceWorld2DSlot::transformPoint(a->worldTransform,a->asset->image.width,a->asset->image.height),"actual action logical far corner stays fixed");
                    const auto slot=index==13||index==18?ibar::layout::ActionButtonSlot::Status:
                        index==14?ibar::layout::ActionButtonSlot::Options:index==19?ibar::layout::ActionButtonSlot::Trade:
                        index==0||index==12?ibar::layout::ActionButtonSlot::General3:ibar::layout::ActionButtonSlot::Main;
                    const auto hit=ibar::layout::actionButtonRect(slot,layout);
                    const auto& im=m->asset->image;const auto& source=a->asset->image;const auto& mat=a->worldTransform.values;
                    for(unsigned y=0;y<im.height;++y)for(unsigned x=0;x<im.width;++x)
                    {
                        const float lx=(x+.5F)/3,ly=(y+.5F)/3;
                        const float wx=lx*mat[0]+ly*mat[3]+mat[6],wy=lx*mat[1]+ly*mat[4]+mat[7];
                        const bool inside=wx>=hit.left&&wx<hit.right&&wy>=hit.top&&wy<hit.bottom;
                        require(im.pixels[(std::size_t(y)*im.width+x)*4+3]==(inside?source.pixels[(std::size_t(y/3)*source.width+x/3)*4+3]:0),
                            "actual source alpha preserved exactly inside immutable interactive band and zero outside");
                    }
                }
                if(state==1 && tick==12){require(replaced,"every actual human/disabled idle action is modernized");++idleProofs;}
                if(state==3 && tick==12 && replaced)++pressedProofs;
                const auto calls=glyphCalls;
                require(modern.sync(*items,cache) && glyphCalls==calls,"same actual action frame reuses O1 source-aware cache");
                context=false;require(modern.sync(*items,cache).has_value(),"context fallback sync");
                for(const auto node:native.order())require(modern.find(node)->asset==native.find(node)->asset,"context rejection restores exact actual retail pointers");
                context=true;
            }
            std::cout<<"actual action qualified root="<<std::hex<<id<<std::dec<<" label="<<caption<<'\n';
        }
        require(idleProofs==18 && pressedProofs>=8,"eighteen actual idle families and settled pressed states qualified");
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
int main(int argc,char** argv)
{
    try{if(argc==3 && std::string_view(argv[1])=="--action-qualify"){testActualActionButtons(argv[2]);return 0;}if(argc==3 && std::string_view(argv[1])=="--action-inspect"){inspectActionButtons(argv[2]);return 0;}testMeasuredStCharlesIdleCard();testPortfolioMiniatures();testMeasuredDeedArtwork();testMeasuredRetailTrade();testMeasuredNavigationAA();testScoreTokenImages();testPurchaseDeedPlacement();std::cout<<"[PASS] measured Trade footprint and context fallback\n";return 0;}
    catch(const std::exception& e){std::cerr<<"[FAIL] "<<e.what()<<'\n';return 1;}
}
