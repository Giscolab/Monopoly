#include "ModernIBarSkin.hpp"
#include "EuropeanDeed.hpp"
#include "FontRuntime.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include "ResourcePaths.hpp"
#include "ResourceRuntime.hpp"
#include "SequenceBitmapRenderData.hpp"
#include "SequenceWorld2DSlot.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
namespace
{
    using namespace monopoly;
    void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
    void benchmarkActualDeeds(const std::filesystem::path& dataRoot,const std::filesystem::path& fontPath)
    {
        using Clock=std::chrono::steady_clock;
        const auto elapsed=[](Clock::time_point start) {
            return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); };
        const auto paths=data::ResourcePaths::create(std::array{std::filesystem::absolute(dataRoot)});
        data::ResourceRuntime resources;
        require(paths && resources.initialize(*paths).has_value(),"Deed benchmark actual DAT initializes");
        const auto snapshot=resources.snapshot();
        require(snapshot->language() && snapshot->language()->catalog,"Deed benchmark actual LANG present");
        fonts::Runtime font;
        require(font.setFont(std::filesystem::absolute(fontPath),"Arial").has_value() &&
            font.setSize(12).has_value(),"Deed benchmark actual Arial initializes");
        font.setWeight(400);
        const auto originalSettings=font.settings();
        data::BitmapRuntimeCache bitmaps;
        unsigned count=0;
        std::cout<<std::setprecision(9)<<"scope=28 actual USA front deeds CPU only; cold derivative/cache; no GPU or FPS\n"
            <<"font="<<fontPath.string()<<" callback=production size/setstyle/renderAA/restore; repeats=1\n";
        for(int square=0;square<40;++square)
        {
            const int index=ibar::layout::propertyIndex(square); if(index<0)continue;
            const auto plan=deeds::plan({square,1,0,13,true,5,true});
            require(plan && !plan->text.empty(),"Canonical production deed plan available");
            const auto name16=snapshot->language()->catalog->lookup(1001U+unsigned(square));
            require(name16 && *name16,"Actual deed LANG name present");
            const auto name=fonts::transcodeUtf8(std::u16string_view(***name16));
            require(name.has_value(),"Actual deed name UTF8 valid");
            const data::DataId root=0x00050CD0+unsigned(index);
            const auto metadata=snapshot->data().metadata(root); const auto bytes=snapshot->data().load(root);
            require(metadata && bytes,"Actual front deed bitmap present");
            const auto decoded=bitmaps.resolve(root,metadata->type,*bytes);
            require(decoded.has_value(),"Actual front deed pixels decode");
            ibar::ModernIBarSkin::DeedDescriptor descriptor;
            for(const auto& fill:plan->fills)
                descriptor.fills.push_back({fill.x,fill.y,fill.width,fill.height,fill.color});
            for(const auto& text:plan->text)
                descriptor.text.push_back({text.text,text.y,text.height,text.justification,text.verticalLeeway,
                    text.fontSize,text.color,text.bold,text.italic,text.verticalCenter});
            descriptor.text.front().text=*name;
            switch(root)
            {
            case 0x50CD2:case 0x50CDA:case 0x50CE1:case 0x50CE9:
                descriptor.artwork=ibar::ModernIBarSkin::DeedArtwork::Railroad;break;
            case 0x50CD7:descriptor.artwork=ibar::ModernIBarSkin::DeedArtwork::Electric;break;
            case 0x50CE4:descriptor.artwork=ibar::ModernIBarSkin::DeedArtwork::Water;break;
            default:break;
            }
            unsigned calls=0; double textMs=0;
            ibar::ModernIBarSkin skin(data::LanguageId::EnglishUs,{});
            skin.configureDeedDescriptors({{root,std::move(descriptor)}},
                [&](std::string_view text,int size,bool bold,bool italic)->std::expected<data::LegacyBitmapRGBA8,std::string>
                {
                    const auto start=Clock::now(); ++calls;
                    auto result=[&]()->std::expected<data::LegacyBitmapRGBA8,std::string>
                    {
                        auto image=font.renderPresentation(text,0xFFFFFF,size,bold?700:400,italic,
                            false,false,true);
                        if(!image)return std::unexpected(image.error().detail);
                        return std::move(*image);
                    }();
                    textMs+=elapsed(start); return result;
                });
            const auto start=Clock::now(); const auto output=skin.substitute(root,*decoded);
            const double coldMs=elapsed(start);
            require(output!=*decoded && output->image.width==597 && output->image.height==681,
                "All28 actual deeds produce qualified complete modern raster");
            require(font.settings()==originalSettings,"Production callback restores every font setting");
            std::uint64_t hash=14695981039346656037ULL;
            for(const auto pixel:output->image.pixels){hash^=pixel;hash*=1099511628211ULL;}
            hash^=output->image.width;hash*=1099511628211ULL;hash^=output->image.height;
            const auto hitStart=Clock::now(); const auto beforeCalls=calls;
            require(skin.substitute(root,*decoded)==output && calls==beforeCalls,"Hot deed cache invokes no font callback");
            const double hitMs=elapsed(hitStart);
            std::cout<<"square="<<square<<" root="<<root<<" native="<<(*decoded)->image.width<<'x'<<(*decoded)->image.height
                <<" output="<<output->image.width<<'x'<<output->image.height<<" callbacks="<<calls
                <<" cold_ms="<<coldMs<<" text_ms="<<textMs<<" cache_ms="<<hitMs<<" fnv1a64="<<hash<<'\n';
            ++count;
        }
        require(count==28,"Benchmark enumerates all28 actual deeds");
    }

    auto bitmap(data::DataId id,unsigned w,unsigned h)
    {
        auto asset=std::make_shared<data::BitmapRuntimeAsset>();
        asset->dataId=id;asset->sourceType=data::LegacyDataType::Uap;
        asset->image={w,h,std::vector<std::uint8_t>(std::size_t(w)*h*4,255)};
        return asset;
    }
    constexpr std::array<unsigned,32> FaceLeaves{
        0x8CB,0x8CC,0x8CD,0x8CE,0x8C8,0x8C9,0x8D1,0x8D2,
        0x8D3,0x8D4,0x8D5,0x8D6,0x8CA,0x8D8,0x86C,0x8DA,
        0x814,0x815,0x816,0x849,0x819,0x81B,0x81C,0x81D,
        0x81E,0x81F,0x820,0x821,0x812,0x823,0x824,0x813};
    void configureFaces(ibar::ModernIBarSkin& skin)
    {
        std::map<data::DataId,ibar::ModernIBarSkin::DrawCardDescriptor> descriptors;
        for(unsigned i=0;i<32;++i)
        {
            const auto idle=i<16?0x50059+i:0x50028+i-16;
            descriptors.emplace(idle,ibar::ModernIBarSkin::DrawCardDescriptor{"Actual title","Actual body",400,idle==0x50029?239U:240U});
        }
        skin.configureDrawCardDescriptors(std::move(descriptors),{});
    }
    void verifyFacePixels(const data::BitmapRuntimeAsset& original,const data::BitmapRuntimeAsset& result)
    {
        require(result.image.width==original.image.width && result.image.height==original.image.height && result.dataId==original.dataId &&
            result.sourceType==data::LegacyDataType::Native && result.presentationRect==original.presentationRect &&
            result.preferLinearFiltering==original.preferLinearFiltering,"FaceIn retains native raster and presentation metadata");
        constexpr std::array<unsigned,3> cream{237,232,215};
        for(std::size_t p=0;p<original.image.pixels.size();p+=4)
        {
            const unsigned r=original.image.pixels[p],g=original.image.pixels[p+1],b=original.image.pixels[p+2];
            const bool neutral=original.image.pixels[p+3]==0 || std::max({r,g,b})<=40 || std::max({r,g,b})-std::min({r,g,b})<=8;
            for(unsigned c=0;c<3;++c)
                require(result.image.pixels[p+c]==(neutral?original.image.pixels[p+c]:(r*cream[c]+127)/255),
                    "FaceIn neutral ink unchanged and warm paper brightness retained");
            require(result.image.pixels[p+3]==original.image.pixels[p+3],"every FaceIn alpha byte unchanged");
        }
    }
    void testCardFaceIn()
    {
        ibar::ModernIBarSkin skin(data::LanguageId::EnglishUs,{});
        bool context=true;skin.configurePresentationContext([&]{return context;});configureFaces(skin);
        for(unsigned i=0;i<33;++i)
        {
            const auto root=i==32?0x5001C:0x50008+i;
            const auto leaf=0x50000+(i==32?0x81A:FaceLeaves[i]);
            auto original=bitmap(leaf,400,240);
            for(std::size_t p=0;p<original->image.pixels.size();p+=4)
            {
                original->image.pixels[p]=240;original->image.pixels[p+1]=110;original->image.pixels[p+2]=15;
                original->image.pixels[p+3]=std::uint8_t(p/4%256);
            }
            original->image.pixels[0]=original->image.pixels[1]=original->image.pixels[2]=0;original->image.pixels[3]=255;
            original->image.pixels[4]=original->image.pixels[5]=original->image.pixels[6]=128;
            require(skin.supportsCardFaceIn(root),"all32 measured FaceIn roots enabled");
            const auto result=skin.substitute(root,original,false);
            require(result!=original,"exact background qualifies even when not principal");verifyFacePixels(*original,*result);
            require(skin.substitute(root,original)==result,"FaceIn source identity cache reused regardless principal");
            context=false;require(skin.substitute(root,original)==original,"context checked before cached FaceIn result");context=true;
            auto bad=std::make_shared<data::BitmapRuntimeAsset>(*original);bad->image.pixels[5]=0;bad->image.pixels[6]=255;
            require(skin.substitute(root,bad)==bad,"unknown coloured artwork rejected");
            bad=std::make_shared<data::BitmapRuntimeAsset>(*original);bad->dataId=0x50636;
            require(skin.substitute(root,bad)==bad,"nonbackground sibling pointer retained");
            bad=bitmap(leaf,400,239);require(skin.substitute(root,bad)==bad,"background extent failclosed");
            bad=std::make_shared<data::BitmapRuntimeAsset>(*original);bad->sourceType=data::LegacyDataType::Bitmap;
            require(skin.substitute(root,bad)==bad,"wrong source type rejected");
            bad=std::make_shared<data::BitmapRuntimeAsset>(*original);bad->image.pixels.pop_back();
            require(skin.substitute(root,bad)==bad,"malformed raster rejected");
        }
        auto blank=bitmap(0x50815,400,240);require(skin.substitute(0x50019,blank)==blank,"neutral-only raster preserved");
        require(!skin.supportsCardFaceIn(0x50028) && !skin.supportsCardFaceIn(0x50007),"adjacent roots not admitted");
        ibar::ModernIBarSkin french(data::LanguageId::French,{});configureFaces(french);french.configurePresentationContext([]{return true;});
        require(!french.supportsCardFaceIn(0x50019),"USA-only art retains foreign fallback");
        skin.configureDrawCardDescriptors({},{});require(!skin.supportsCardFaceIn(0x50019),"unconfigured owner restores fallback");
    }
    void testActualCardFaces(const std::filesystem::path& root,bool inspect=false)
    {
        const auto paths=data::ResourcePaths::create(std::array{root});data::ResourceRuntime resources;
        require(paths && resources.initialize(*paths).has_value(),"actual card DAT opens");
        const auto snapshot=resources.snapshot();auto skin=std::make_shared<ibar::ModernIBarSkin>(data::LanguageId::EnglishUs,ibar::ModernIBarSkin::TextRasterizer{});
        bool context=true;skin->configurePresentationContext([&]{return context;});configureFaces(*skin);
        data::BitmapRuntimeCache cache;unsigned replaced=0,siblings=0,warmSprites=0,neutralSprites=0;
        for(unsigned i=0;i<33;++i)
        {
            const auto owner=i==32?0x5001C:0x50008+i,leaf=0x50000+(i==32?0x81A:FaceLeaves[i]);
            const auto metadata=snapshot->data().metadata(leaf);const auto bytes=snapshot->data().load(leaf);
            require(metadata && bytes,"actual background metadata and bytes load");
            const auto original=cache.resolve(leaf,metadata->type,*bytes);require(original.has_value(),"actual background decodes");
            const auto modern=skin->substitute(owner,*original);
            require(modern!=*original,"all33 actual background palettes qualify");verifyFacePixels(**original,*modern);
        }
        for(unsigned i=0;i<32;++i)
        {
            const auto owner=0x50008+i;
            const auto program=sequence::SequenceProgram::load(snapshot,owner,0);require(program.has_value(),"actual FaceIn CNK loads");
            std::set<data::DataId> leaves;
            for(const auto& description:(*program)->descriptions())
                if(std::holds_alternative<data::SequenceBitmapData>(description.record.data) && description.contentsDataId)
                    leaves.insert(*description.contentsDataId);
            for(const auto leaf:leaves)
            {
                if(leaf==0x50000+FaceLeaves[i] || (owner==0x5001C && leaf==0x5081A)) continue;
                const auto metadata=snapshot->data().metadata(leaf);const auto bytes=snapshot->data().load(leaf);
                require(metadata && bytes,"every actual sprite description loads");
                const auto original=cache.resolve(leaf,metadata->type,*bytes);require(original.has_value(),"every actual sprite decodes");
                const auto uap=data::inspectLegacyUap(**bytes);require(uap.has_value(),"every sprite has actual native UAP metadata");
                bool warm=false;
                for(std::size_t p=0;p<(*original)->image.pixels.size();p+=4)
                {
                    const auto& pixels=(*original)->image.pixels;
                    const unsigned r=pixels[p],g=pixels[p+1],b=pixels[p+2];
                    if(!pixels[p+3] || std::max({r,g,b})<=40 || std::max({r,g,b})-std::min({r,g,b})<=8) continue;
                    require(r+8>=g && g+8>=b,"production sprite has no unknown visible palette colour");warm=true;
                }
                const auto result=skin->substitute(owner,*original,false);
                if(warm)
                {
                    require(result!=*original,"every measured warm sprite qualifies");verifyFacePixels(**original,*result);++warmSprites;
                    require(skin->substitute(owner,*original)==result,"actual sprite cache preserves immutable identity");
                    context=false;require(skin->substitute(owner,*original)==*original,"actual sprite cache respects current context");context=true;
                    auto bad=std::make_shared<data::BitmapRuntimeAsset>(**original);
                    auto raw=std::make_shared<data::DataBytes>(**bytes);
                    (*raw)[4]=std::byte{0xFF};(*raw)[5]=std::byte{0x7F};bad->source=raw;
                    require(skin->substitute(owner,bad)==bad,"actual sprite wrong intrinsic origin fails closed");
                    bad=std::make_shared<data::BitmapRuntimeAsset>(**original);++bad->image.width;
                    bad->image.pixels.resize(std::size_t(bad->image.width)*bad->image.height*4,255);
                    require(skin->substitute(owner,bad)==bad,"actual sprite wrong native extent fails closed");
                    bad=std::make_shared<data::BitmapRuntimeAsset>(**original);bad->dataId=0x50999;
                    require(skin->substitute(owner,bad)==bad,"actual sprite unknown ID fails closed");
                    bad=std::make_shared<data::BitmapRuntimeAsset>(**original);bad->image.pixels[0]=0;bad->image.pixels[1]=0;bad->image.pixels[2]=255;bad->image.pixels[3]=255;
                    require(skin->substitute(owner,bad)==bad,"actual sprite unexpected visible palette fails closed");
                    if(inspect)std::cout<<"    {0x"<<std::hex<<data::dataTag(owner)<<",0x"<<data::dataTag(leaf)<<std::dec<<','<<uap->width<<','<<uap->height<<','<<uap->originX<<','<<uap->originY<<"},\n";
                }
                else {require(result==*original,"all neutral-only production sprites retain exact pointer");++neutralSprites;}
            }
            sequence::SequenceRuntime runtime;require(runtime.start(*program,1005).has_value(),"actual FaceIn starts");
            engine::SequenceWorld2DSlot native,modern;modern.configureModernIBarSkin(skin);
            bool ownerReplaced=false;
            for(int tick=0;tick<=24;++tick)
            {
                require(runtime.update(tick).has_value(),"actual FaceIn advances");
                if(tick!=0 && tick!=4 && tick!=12 && tick!=24)continue;
                const auto items=sequence::collectSequenceBitmapRenderData(runtime,snapshot);
                require(items && native.sync(*items,cache) && modern.sync(*items,cache),"actual FaceIn publishes both slots");
                require(native.order()==modern.order(),"FaceIn leaf ordering retained");
                for(const auto node:native.order())
                {
                    const auto* a=native.find(node);const auto* m=modern.find(node);
                    require(a && m && a->clock==m->clock && a->priority==m->priority && a->contentsDataId==m->contentsDataId &&
                        a->worldTransform.values==m->worldTransform.values,"FaceIn clock priority identity and transform retained");
                    const bool background=a->contentsDataId==0x50000+FaceLeaves[i] || (owner==0x5001C && a->contentsDataId==0x5081A);
                    if(background){require(a->asset!=m->asset,"actual background changes");verifyFacePixels(*a->asset,*m->asset);ownerReplaced=true;++replaced;}
                    else
                    {
                        const auto expected=skin->substitute(owner,a->asset,false);
                        require(m->asset==expected,"actual sprite follows exact per-leaf qualification");
                        if(expected!=a->asset)verifyFacePixels(*a->asset,*m->asset);
                        else ++siblings;
                    }
                }
                context=false;require(modern.sync(*items,cache).has_value(),"actual context fallback publishes");
                for(const auto node:native.order())require(native.find(node)->asset==modern.find(node)->asset,"actual cached context fallback exact");
                context=true;
            }
            require(ownerReplaced,"each actual FaceIn owner exposes a qualified background");
        }
        require(replaced>=32 && warmSprites==1151 && neutralSprites==60,"actual33 backgrounds and all1211 production sprites qualified");
        if(!inspect)std::cout<<"[PASS] actual32 FaceIn roots,33 backgrounds,1151 warm sprites,60 neutral sprites; replaced background samples="<<replaced<<" retained sprite samples="<<siblings<<'\n';
    }
    void testChanceNativeIdle()
    {
        bool context=true; unsigned textCalls=0;
        ibar::ModernIBarSkin skin(data::LanguageId::EnglishUs,{});
        skin.configurePresentationContext([&]{return context;});
        skin.configureDrawCardDescriptors({{0x50037,{"Chance","Go back 3 spaces.",400,240}}},
            [&](std::string_view,int,bool,bool)->std::expected<data::LegacyBitmapRGBA8,std::string> {
                ++textCalls;return std::unexpected("native artwork must not render duplicate text");
            });
        auto original=bitmap(0x50991,400,240);
        for(std::size_t i=0;i<original->image.pixels.size();i+=4)
        { original->image.pixels[i]=255;original->image.pixels[i+1]=128;original->image.pixels[i+2]=0; }
        original->image.pixels[0]=original->image.pixels[1]=original->image.pixels[2]=0;
        original->image.pixels[4]=original->image.pixels[5]=original->image.pixels[6]=72;
        original->image.pixels[7]=137;
        original->image.pixels[11]=0;
        const auto before=original->image.pixels;
        const auto result=skin.substitute(0x50037,original);
        require(result!=original && textCalls==0,"Chance15 restores native artwork without generating title or caption");
        verifyFacePixels(*original,*result);
        require(original->image.pixels==before && result->source==original->source,
            "native card retains source provenance and original immutable pixels");
        require(skin.substitute(0x50037,original)==result,"native card artwork cache uses immutable source identity");
        context=false;require(skin.substitute(0x50037,original)==original,"context guard precedes native artwork cache");context=true;
        require(skin.substitute(0x50037,original,false)==original,"unqualified sibling retains complete retail artwork");
        for(const auto dimensions:{std::array<unsigned,2>{399,240},std::array<unsigned,2>{400,239}})
        {
            const auto wrong=bitmap(0x50991,dimensions[0],dimensions[1]);
            require(skin.substitute(0x50037,wrong)==wrong,"native artwork rejects changed authored extent");
        }
        auto wrong=std::make_shared<data::BitmapRuntimeAsset>(*original);wrong->dataId=0x50990;
        require(skin.substitute(0x50037,wrong)==wrong,"native artwork rejects a different source leaf");
        wrong=std::make_shared<data::BitmapRuntimeAsset>(*original);wrong->sourceType=data::LegacyDataType::Bitmap;
        require(skin.substitute(0x50037,wrong)==wrong,"native artwork requires measured UAP provenance");
        wrong=std::make_shared<data::BitmapRuntimeAsset>(*original);wrong->image.pixels.resize(4);
        require(skin.substitute(0x50037,wrong)==wrong,"malformed native artwork falls back before palette access");
        wrong=std::make_shared<data::BitmapRuntimeAsset>(*original);
        wrong->image.pixels[20]=0;wrong->image.pixels[21]=128;wrong->image.pixels[22]=255;
        require(skin.substitute(0x50037,wrong)==wrong,"unexpected authored colour remains complete retail artwork");
        auto changed=std::make_shared<data::BitmapRuntimeAsset>(*original);changed->image.pixels[15]=91;
        const auto changedResult=skin.substitute(0x50037,changed);
        require(changedResult!=result && changedResult!=changed && changedResult->image.pixels[15]==91 &&
            result->image.pixels[15]==before[15],"new immutable source alpha creates a separate derivative without changing old artwork");
        ibar::ModernIBarSkin french(data::LanguageId::French,{});
        french.configurePresentationContext([]{return true;});
        french.configureDrawCardDescriptors({{0x50037,{"Chance","Corps",400,240}}},
            [](std::string_view,int,bool,bool)->std::expected<data::LegacyBitmapRGBA8,std::string>{return std::unexpected("unused");});
        require(french.substitute(0x50037,original)==original,"other language artwork retains retail fallback");
    }

    void testAllNativeIdleSources()
    {
        unsigned textCalls=0;
        ibar::ModernIBarSkin skin(data::LanguageId::EnglishUs,{});
        skin.configurePresentationContext([]{return true;});
        std::map<data::DataId,ibar::ModernIBarSkin::DrawCardDescriptor> descriptors;
        for(unsigned index=0;index<32;++index)
        {
            const auto owner=index<16?0x50028+index:0x50059+index-16;
            descriptors.emplace(owner,ibar::ModernIBarSkin::DrawCardDescriptor{"Title","Caption",400,index==1?239U:240U});
        }
        skin.configureDrawCardDescriptors(std::move(descriptors),
            [&](std::string_view,int,bool,bool)->std::expected<data::LegacyBitmapRGBA8,std::string>{++textCalls;return std::unexpected("duplicate text");});
        for(unsigned index=0;index<32;++index)
        {
            const auto owner=index<16?0x50028+index:0x50059+index-16;
            const auto leaf=index<16?0x50982+index:0x50972+index-16;
            const unsigned height=index==1?239:240;
            const auto original=bitmap(leaf,400,height);
            for(std::size_t p=0;p<original->image.pixels.size();p+=4)
            {original->image.pixels[p]=255;original->image.pixels[p+1]=128;original->image.pixels[p+2]=0;}
            original->image.pixels[0]=original->image.pixels[1]=original->image.pixels[2]=0;
            const auto result=skin.substitute(owner,original);
            require(result!=original,"all32 exact root/leaf pairs recover native illustrated artwork");
            verifyFacePixels(*original,*result);
            auto wrong=std::make_shared<data::BitmapRuntimeAsset>(*original);wrong->dataId=leaf+1;
            require(skin.substitute(owner,wrong)==wrong,"every idle owner rejects a different leaf");
            wrong=bitmap(leaf,400,height==239?240:239);
            require(skin.substitute(owner,wrong)==wrong,"every idle owner requires its exact authored height");
        }
        require(textCalls==0,"all32 recovered idle cards retain original captions without font calls");
    }

    void testActualNativeIdleCards(const std::filesystem::path& root,bool chanceOnly=false)
    {
        const auto paths=data::ResourcePaths::create(std::array{root});
        data::ResourceRuntime resources;
        require(paths && resources.initialize(*paths).has_value(),"licensed resources open for native idle artwork qualification");
        std::map<data::DataId,ibar::ModernIBarSkin::DrawCardDescriptor> descriptors;
        for(unsigned index=0;index<32;++index)
            descriptors.emplace(index<16?0x50028+index:0x50059+index-16,
                ibar::ModernIBarSkin::DrawCardDescriptor{"Title","Caption",400,index==1?239U:240U});
        auto skin=std::make_shared<ibar::ModernIBarSkin>(data::LanguageId::EnglishUs,ibar::ModernIBarSkin::TextRasterizer{});
        bool context=true;skin->configurePresentationContext([&]{return context;});
        unsigned textCalls=0;
        skin->configureDrawCardDescriptors(std::move(descriptors),
            [&](std::string_view,int,bool,bool)->std::expected<data::LegacyBitmapRGBA8,std::string>{++textCalls;return std::unexpected("native art must retain printed text");});
        data::BitmapRuntimeCache cache;
        for(unsigned index=chanceOnly?15U:0U;index<(chanceOnly?16U:32U);++index)
        {
        const auto owner=index<16?0x50028+index:0x50059+index-16;
        const auto expectedLeaf=index<16?0x50982+index:0x50972+index-16;
        const unsigned expectedHeight=index==1?239:240;
        sequence::SequenceRuntime runtime;
        const auto program=sequence::SequenceProgram::load(resources.snapshot(),owner);
        require(program && runtime.start(*program,1005).has_value(),"production illustrated idle root starts");
        require(runtime.update(0).has_value(),"production card child bitmap reaches its authored start");
        const auto leaves=sequence::collectSequenceBitmapRenderData(runtime,resources.snapshot());
        require(leaves && leaves->size()==1 && leaves->front().contentsDataId==expectedLeaf,
            "actual idle card is the measured single illustrated source leaf");
        const auto& leaf=leaves->front();
        const auto original=cache.resolve(leaf.contentsDataId,leaf.metadata.type,leaf.bytes);
        require(original.has_value(),"licensed illustrated bitmap decodes");
        require((*original)->sourceType==data::LegacyDataType::Uap && (*original)->image.width==400 &&
            (*original)->image.height==expectedHeight,"actual source provenance and dimensions match exact card contract");
        const auto result=skin->substitute(owner,*original);
        require(result!=*original,"actual production palette qualifies native artwork recovery");
        verifyFacePixels(**original,*result);
        engine::SequenceWorld2DSlot retail;
        require(retail.sync(*leaves,cache).has_value(),"actual unskinned card establishes authored raster placement");
        const auto* retailObject=retail.find(leaf.node);
        require(retailObject && retailObject->asset==*original,"retail baseline retains decoded illustrated bitmap");
        engine::SequenceWorld2DSlot slot;slot.configureModernIBarSkin(skin);
        require(slot.sync(*leaves,cache).has_value(),"native derivative reaches actual sequence slot");
        const auto* object=slot.find(leaf.node);
        require(object && object->clock==leaf.clock && object->priority==leaf.priority &&
            object->contentsDataId==leaf.contentsDataId && object->asset->image.width==retailObject->asset->image.width &&
            object->asset->image.height==retailObject->asset->image.height &&
            object->worldTransform.values==retailObject->worldTransform.values,
            "artwork recovery preserves actual owner, clock, priority, identity and native placement");
        std::cout<<"[PASS] actual raster placement nativeXY="<<retailObject->worldTransform.values[6]<<','
            <<retailObject->worldTransform.values[7]<<" sequenceXY="<<leaf.worldTransform.values[6]<<','
            <<leaf.worldTransform.values[7]<<" explicit_bounds="<<leaf.bounds.has_value()<<'\n';
        require(skin->substitute(owner,*original)==result,"production native artwork reuses immutable cached derivative");
        context=false;require(skin->substitute(owner,*original)==*original,"actual context change restores retail artwork even after cache hit");context=true;
        std::cout<<"[PASS] native idle card="<<index<<" root="<<owner<<" leaf="<<expectedLeaf
            <<" extent=400x"<<expectedHeight<<" exact_ink_alpha=true\n";
        }
        require(textCalls==0,"actual native idle artwork never duplicates title/body text");
        std::cout<<"[PASS] licensed native idle artwork qualification count="<<(chanceOnly?1:32)<<'\n';
    }



    void testActualLoanFooter(const std::filesystem::path& root,const std::filesystem::path& fontPath,bool allQualified=false)
    {
        const auto paths=data::ResourcePaths::create(std::array{root});data::ResourceRuntime resources;
        require(paths && resources.initialize(*paths),"actual qualified card DAT opens");
#include "ModernDrawCardText.inc"
        fonts::Runtime font;require(font.setFont(fontPath,"Arial").has_value(),"actual production Arial loads for qualified card");
        const auto settings=font.settings();
        struct Card {unsigned index;data::DataId owner,leaf,outRoot,outLeaf;};
        constexpr std::array<Card,5> cases{{{3,0x5002B,0x50985,0x5004B,0x5083C},
            {20,0x5005D,0x50976,0x5007D,0x508EF},{22,0x5005F,0x50978,0x5007F,0x508F1},
            {23,0x50060,0x50979,0x50080,0x508F2},{25,0x50062,0x5097B,0x50082,0x508F4}}};
        for(unsigned cardIndex=0;cardIndex<(allQualified?cases.size():1U);++cardIndex)
        {
        const auto selected=cases[cardIndex];const auto owner=selected.owner,leaf=selected.leaf;
        const auto body=ModernUsaDrawCardBodies[selected.index];
        const auto title=selected.index<16?"Chance":"Community Chest";
        unsigned calls=0;bool fail=false,oversized=false;
        std::vector<std::string> texts;std::vector<std::pair<std::string,unsigned>> measurements;
        const auto raster=[&](std::string_view text,int size,bool bold,bool italic)->std::expected<data::LegacyBitmapRGBA8,std::string>
        {
            ++calls;texts.emplace_back(text);require(size==48 && !bold && !italic,"qualified card uses readable16px production body style");
            if(fail)return std::unexpected("qualified font failure");
            if(oversized)return data::LegacyBitmapRGBA8{1105,157,std::vector<std::uint8_t>(1105*157*4,255)};
            auto value=font.renderPresentation(text,0xFFFFFF,size,400,italic);
            if(!value)return std::unexpected(value.error().detail);measurements.emplace_back(text,value->width);return std::move(*value);
        };
        auto skin=std::make_shared<ibar::ModernIBarSkin>(data::LanguageId::EnglishUs,ibar::ModernIBarSkin::TextRasterizer{});
        bool context=true;skin->configurePresentationContext([&]{return context;});
        const auto configure=[&]{skin->configureDrawCardDescriptors({{owner,{title,std::string(body),400,240}}},raster);};configure();
        const auto bytes=resources.snapshot()->data().load(leaf);require(bool(bytes),"actual qualified card raw payload loads");data::BitmapRuntimeCache cache;
        const auto source=cache.resolve(leaf,data::LegacyDataType::Uap,*bytes);require(bool(source),"exact measured opaque illustrated source decodes");
        const auto originalPixels=(*source)->image.pixels;
        const auto result=skin->substitute(owner,*source);
        require(result!=*source && result->image.width==1200 && result->image.height==720 && result->preferLinearFiltering &&
            result->source==(*source)->source && result->dataId==(*source)->dataId,"qualified actual card retains provenance in3x400x240 derivative");
        std::string acceptedBody;unsigned wraps=0;
        for(std::size_t i=0;i<measurements.size();++i)if(measurements[i].second>368*3)
        {require(i>0,"actual line overflow has a previously measured fitting line");acceptedBody=measurements[i-1].first+" ";++wraps;}
        require(!texts.empty() && wraps<=1 && acceptedBody+texts.back()==body,
            "complete canonical body is rendered as one or two actual production-font lines");
        ibar::ModernIBarSkin nativeSkin(data::LanguageId::EnglishUs,{});nativeSkin.configurePresentationContext([]{return true;});
        nativeSkin.configureDrawCardDescriptors({{owner,{title,"Native ink reference",400,240}}},raster);
        const auto tinted=nativeSkin.substitute(owner,*source);
        bool fullArt=true,alpha=true,footerInk=false;
        for(unsigned y=0;y<720;++y)for(unsigned x=0;x<1200;++x)
        {
            const auto at=(std::size_t(y)*1200+x)*4;alpha &= result->image.pixels[at+3]==255;
            if(y<540 && x>=150 && x<1050)
            {
                const auto original=(std::size_t(y*240/540)*400+(x-150)*400/900)*4;
                fullArt &= std::equal(result->image.pixels.begin()+at,result->image.pixels.begin()+at+4,tinted->image.pixels.begin()+original);
            }
            if(y>=552 && y<708 && x>=48 && x<1152)footerInk |= result->image.pixels[at]<100;
        }
        require(fullArt && alpha && footerInk && (*source)->image.pixels==originalPixels,
            "complete actual artwork maps without crop, readable footer adds ink, source/opaque alpha immutable");
        const auto previous=calls;require(skin->substitute(owner,*source)==result && calls==previous,"warm complete loan artwork calls no font");
        context=false;require(skin->substitute(owner,*source)==*source,"live context fallback checked before cached composition");context=true;
        auto invalid=std::make_shared<data::BitmapRuntimeAsset>(**source);invalid->source.reset();
        require(skin->substitute(owner,invalid)==invalid,"missing immutable raw header returns whole native source");
        invalid=std::make_shared<data::BitmapRuntimeAsset>(**source);invalid->source=std::make_shared<const data::DataBytes>(1,std::byte{});
        require(skin->substitute(owner,invalid)==invalid,"malformed raw UAP header returns whole native source");
        invalid=std::make_shared<data::BitmapRuntimeAsset>(**source);invalid->image.pixels[3]=128;
        require(skin->substitute(owner,invalid)==invalid,"partial native alpha rejects composition entirely");
        invalid=std::make_shared<data::BitmapRuntimeAsset>(**source);invalid->dataId=leaf+1;
        require(skin->substitute(owner,invalid)==invalid,"different illustrated source cannot receive qualified card footer");
        fail=true;configure();require(skin->substitute(owner,*source)==*source,"font failure returns complete native bitmap");fail=false;
        oversized=true;configure();require(skin->substitute(owner,*source)==*source,"unreadable footer fit returns complete native bitmap");oversized=false;configure();
        for(const int y:{0,136})
        {
            sequence::SequenceRuntime runtime;const auto program=sequence::SequenceProgram::load(resources.snapshot(),owner);
            require(program && runtime.start(*program,1005,{},sequence::SequenceTransform(sequence::translate2D(0,float(y)))) && runtime.update(0),"actual loan CNK starts with authored root placement");
            const auto leaves=sequence::collectSequenceBitmapRenderData(runtime,resources.snapshot());require(bool(leaves),"actual loan leaves collect");
            engine::SequenceWorld2DSlot native,modern;modern.configureModernIBarSkin(skin);
            require(native.sync(*leaves,cache) && modern.sync(*leaves,cache),"actual loan reaches production Slot");
            const auto node=leaves->front().node;const auto* a=native.find(node);const auto* b=modern.find(node);
            for(const auto corner:std::array<std::array<int,2>,2>{{{0,0},{1,1}}})
                require(engine::SequenceWorld2DSlot::transformPoint(a->worldTransform,corner[0]*400,corner[1]*240)==
                    engine::SequenceWorld2DSlot::transformPoint(b->worldTransform,corner[0]*1200,corner[1]*720),"caption composition keeps exact original logical corners");
            require(a->clock==b->clock && a->priority==b->priority && a->contentsDataId==b->contentsDataId && leaves->front().rootSequencePriority==1005,
                "root1005/leaf1 clock/provenance unchanged by footer");
            skin->configureIdleCardPresentation([](data::DataId,std::uint16_t,const sequence::Matrix2D& world)
                ->std::optional<ibar::ModernIBarSkin::IdleCardPresentation>
                {return ibar::ModernIBarSkin::IdleCardPresentation{{600,52.5F-world.values[7],800,172.5F-world.values[7]},false};});
            require(modern.sync(*leaves,cache).has_value(),"qualified card footer also fits existing pass48 reflow");
            b=modern.find(node);require(engine::SequenceWorld2DSlot::transformPoint(b->worldTransform,0,0)==std::array<std::int32_t,2>{600,52} &&
                engine::SequenceWorld2DSlot::transformPoint(b->worldTransform,1200,720)==std::array<std::int32_t,2>{800,172},"pass48 target remains exact at both native origins");
            skin->configureIdleCardPresentation({});
        }
        const auto outBytes=resources.snapshot()->data().load(selected.outLeaf);
        require(outBytes && **outBytes==**bytes,"exact outgoing source is raw-byte-identical to measured idle face");
        const auto outSource=cache.resolve(selected.outLeaf,data::LegacyDataType::Uap,*outBytes);
        require(bool(outSource),"exact outgoing illustrated face decodes");
        require(skin->substitute(selected.outRoot,*source)==*source,"idle leaf cannot masquerade as exact outgoing owner");
        const auto outgoing=skin->substitute(selected.outRoot,*outSource);
        require(outgoing!=*outSource && outgoing->image.pixels==result->image.pixels &&
            outgoing->source==(*outSource)->source && outgoing->dataId==selected.outLeaf,
            "idle and exact Out use identical complete composition with their own immutable source identity");
        const auto outProgram=sequence::SequenceProgram::load(resources.snapshot(),selected.outRoot);
        sequence::SequenceRuntime outRuntime;
        require(outProgram && outRuntime.start(*outProgram,1005,{},sequence::SequenceTransform(sequence::translate2D(0,136))),
            "actual outgoing program starts at native CardPriority/Y136");
        engine::SequenceWorld2DSlot outNative,outModern;outModern.configureModernIBarSkin(skin);
        for(const int tick:{0,4,8})
        {
            require(outRuntime.update(tick).has_value(),"actual Out clock advances");
            const auto items=sequence::collectSequenceBitmapRenderData(outRuntime,resources.snapshot());
            require(items && outNative.sync(*items,cache) && outModern.sync(*items,cache) && outNative.order()==outModern.order(),
                "actual Out leaves preserve authored node order");
            unsigned changed=0;
            for(const auto& item:*items)
            {
                const auto* a=outNative.find(item.node);const auto* b=outModern.find(item.node);
                require(a->clock==b->clock && a->priority==b->priority && a->contentsDataId==b->contentsDataId,
                    "every actual Out leaf retains its clock priority and source ID");
                if(item.contentsDataId==selected.outLeaf)
                {
                    ++changed;require(b->asset->image.pixels==result->image.pixels && !b->asset->presentationRect,
                        "exact Out face reuses coherent artwork without idle Trade reflow");
                    for(const auto corner:std::array<std::array<int,2>,2>{{{0,0},{1,1}}})
                        require(engine::SequenceWorld2DSlot::transformPoint(a->worldTransform,corner[0]*400,corner[1]*240)==
                            engine::SequenceWorld2DSlot::transformPoint(b->worldTransform,corner[0]*1200,corner[1]*720),
                            "every moving Out pose keeps exact original logical corners");
                }
                else require(a->asset==b->asset && a->worldTransform.values==b->worldTransform.values,
                    "authored Out companion remains exact native pointer and matrix");
            }
            require(changed==1,"only the proven outgoing face receives readable composition");
            context=false;require(outModern.sync(*items,cache).has_value(),"outgoing context fallback publishes");
            for(const auto node:outNative.order())require(outModern.find(node)->asset==outNative.find(node)->asset,
                "outgoing fallback restores all native assets");context=true;
        }
        std::cout<<"[PASS] readable card global="<<selected.index<<" idle="<<owner<<" out="<<selected.outRoot
            <<" measured identical source, complete caption and authored moving poses\n";
        }
        require(font.settings()==settings,"production presentation raster preserves shared font state");
        std::cout<<"[PASS] qualified fullart/canonical footer/cache/guards/font/native corners/pass48 and exact Out\n";
    }

    void testActualIdleCardTrade(const std::filesystem::path& root)
    {
        const auto paths=data::ResourcePaths::create(std::array{root});data::ResourceRuntime resources;
        require(paths && resources.initialize(*paths),"actual idle reflow DAT opens");
        auto skin=std::make_shared<ibar::ModernIBarSkin>(data::LanguageId::EnglishUs,ibar::ModernIBarSkin::TextRasterizer{});
        std::map<data::DataId,ibar::ModernIBarSkin::DrawCardDescriptor> descriptors;
        for(unsigned i=0;i<32;++i)descriptors.emplace(i<16?0x50028+i:0x50059+i-16,
            ibar::ModernIBarSkin::DrawCardDescriptor{"","",400,i==1?239U:240U});
        skin->configureDrawCardDescriptors(std::move(descriptors),[](std::string_view,int,bool,bool)->std::expected<data::LegacyBitmapRGBA8,std::string>{throw std::runtime_error("native art must not render captions");});
        bool context=true,trade=false,hover=false;skin->configurePresentationContext([&]{return context;});
        skin->configureIdleCardPresentation([&](data::DataId owner,std::uint16_t priority,const sequence::Matrix2D& world)
            ->std::optional<ibar::ModernIBarSkin::IdleCardPresentation>
        {
            require(priority==1005,"provider receives actual card priority");if(!trade)return {};
            const float height=owner==0x50029?239.0F:240.0F;
            return ibar::ModernIBarSkin::IdleCardPresentation{{600,(225-height/2)/2-world.values[7],
                800,(225+height/2)/2-world.values[7]},hover};
        });
        data::BitmapRuntimeCache cache;
        for(unsigned i=0;i<32;++i)for(const int y:{0,136})
        {
            const auto owner=i<16?0x50028+i:0x50059+i-16;
            sequence::SequenceRuntime runtime;const auto program=sequence::SequenceProgram::load(resources.snapshot(),owner);
            require(program && runtime.start(*program,1005,{},sequence::SequenceTransform(sequence::translate2D(0,float(y)))) && runtime.update(0),
                "actual idle program starts at native Main or Trade origin");
            const auto leaves=sequence::collectSequenceBitmapRenderData(runtime,resources.snapshot());
            require(leaves && leaves->size()==1,"actual single idle artwork leaf collected");
            engine::SequenceWorld2DSlot slot;slot.configureModernIBarSkin(skin);
            trade=false;require(slot.sync(*leaves,cache).has_value(),"native Main cream card publishes");
            require(leaves->front().rootSequencePriority==1005 && leaves->front().priority==1,"actual root1005 and leaf1 remain distinct");
            const auto node=leaves->front().node;const auto main=*slot.find(node);const auto pixels=main.asset->image.pixels;
            trade=true;require(slot.sync(*leaves,cache).has_value(),"Trade card reflows through actual Slot");
            const auto fitted=*slot.find(node);const float h=i==1?239.0F:240.0F;
            const auto a=engine::SequenceWorld2DSlot::transformPoint(fitted.worldTransform,0,0);
            const auto b=engine::SequenceWorld2DSlot::transformPoint(fitted.worldTransform,400,int(h));
            require(a[0]==600 && a[1]==int(std::nearbyint((225-h/2)/2)) && b[0]==800 && b[1]==int(std::nearbyint((225+h/2)/2)),
                "both native origins land in the same right-top200-wide panel");
            require(fitted.asset->image.pixels==pixels && fitted.clock==main.clock && fitted.priority==main.priority &&
                fitted.contentsDataId==main.contentsDataId && fitted.asset->source==main.asset->source,
                "reflow retains full native RGBA provenance clock priority and leaf identity");
            require(slot.sync(*leaves,cache) && slot.find(node)->asset==fitted.asset,"warm reflow reuses immutable derivative");
            hover=true;require(slot.sync(*leaves,cache).has_value(),"actual hover presentation proxy publishes");
            const auto hidden=slot.find(node)->asset;bool retained=true;
            for(std::size_t at=0;at<pixels.size();at+=4)
                retained &= hidden->image.pixels[at]==pixels[at] && hidden->image.pixels[at+1]==pixels[at+1] &&
                    hidden->image.pixels[at+2]==pixels[at+2] && hidden->image.pixels[at+3]==0;
            require(retained && fitted.asset->image.pixels==pixels,"only proxy alpha changes and retired artwork is immutable");
            hover=false;require(slot.sync(*leaves,cache) && slot.find(node)->asset==fitted.asset,"hover end restores cached artwork");
            trade=false;require(slot.sync(*leaves,cache) && slot.find(node)->asset->image.pixels==main.asset->image.pixels &&
                slot.find(node)->worldTransform.values==main.worldTransform.values,"Main restores exact native presentation");
            context=false;require(slot.sync(*leaves,cache) && !slot.find(node)->asset->presentationRect,"unqualified context restores retail geometry");context=true;
            auto source=cache.resolve(leaves->front().contentsDataId,leaves->front().metadata.type,leaves->front().bytes);
            trade=true;auto wrong=sequence::translate2D(1,float(y));
            require(skin->substitute(owner,*source,true,{},1005,wrong)==*source,"unqualified sequence placement fails closed");
            const auto ignored=skin->substitute(owner,*source,true,{},1004,leaves->front().worldTransform);
            require(!ignored->presentationRect,"wrong priority cannot reflow");
        }
        require(!skin->supportsIdleCardPresentation(0x50087) && !skin->supportsIdleCardPresentation(0x50047),
            "Out and FaceIn animation owners never reflow");
        std::cout<<"[PASS] actual32 idle cards two native origins, hover proxy, restore and fallback\n";
    }

    void testActualNativeOutCards(const std::filesystem::path& root)
    {
        const auto paths=data::ResourcePaths::create(std::array{root});data::ResourceRuntime resources;
        require(paths && resources.initialize(*paths),"actual resources open for outgoing art qualification");
        auto skin=std::make_shared<ibar::ModernIBarSkin>(data::LanguageId::EnglishUs,ibar::ModernIBarSkin::TextRasterizer{});
        std::map<data::DataId,ibar::ModernIBarSkin::DrawCardDescriptor> descriptors;
        for(unsigned index=0;index<32;++index)descriptors.emplace(index<16?0x50028+index:0x50059+index-16,
            ibar::ModernIBarSkin::DrawCardDescriptor{"Actual title","Actual ink",400,index==1?239U:240U});
        bool context=true;unsigned textCalls=0;skin->configurePresentationContext([&]{return context;});
        skin->configureDrawCardDescriptors(std::move(descriptors),
            [&](std::string_view,int,bool,bool)->std::expected<data::LegacyBitmapRGBA8,std::string>{++textCalls;return std::unexpected("no duplicate native caption");});
        const auto snapshot=resources.snapshot();data::BitmapRuntimeCache cache;unsigned identical=0;
        for(unsigned index=0;index<32;++index)
        {
            const auto owner=index<16?0x50048+index:0x50079+index-16;
            const auto outgoingLeaf=index<16?0x50839+index:0x508EB+index-16;
            const auto idleLeaf=index<16?0x50982+index:0x50972+index-16;
            const auto outBytes=snapshot->data().load(outgoingLeaf),idleBytes=snapshot->data().load(idleLeaf);
            require(outBytes && idleBytes,"both exact actual outgoing/idle sources exist");
            const auto source=cache.resolve(outgoingLeaf,data::LegacyDataType::Uap,*outBytes);
            require(source.has_value(),"actual outgoing source decodes");
            if(index==12)
            {
                require(**outBytes!=**idleBytes && !skin->supports(owner) && skin->substitute(owner,*source)==*source,
                    "differing Poor Tax outgoing art remains complete retail fallback");continue;
            }
            require(**outBytes==**idleBytes,"qualified outgoing payload is byte-identical to actual idle artwork");++identical;
            const auto result=skin->substitute(owner,*source);require(result!=*source,"exact outgoing illustrated pair qualifies");
            verifyFacePixels(**source,*result);
            require(skin->substitute(owner,*source)==result && skin->substitute(owner,*source,false)==*source,
                "outgoing artwork cache reuses exact owner while secondary leaves remain native");
            auto bad=std::make_shared<data::BitmapRuntimeAsset>(**source);bad->source.reset();
            require(skin->substitute(owner,bad)==bad,"outgoing art requires immutable raw UAP provenance");
            bad=std::make_shared<data::BitmapRuntimeAsset>(**source);bad->dataId=idleLeaf;
            require(skin->substitute(owner,bad)==bad,"idle leaf cannot masquerade as outgoing owner artwork");
            bad=std::make_shared<data::BitmapRuntimeAsset>(**source);bad->image.height=index==1?240:239;
            require(skin->substitute(owner,bad)==bad,"each outgoing pair requires exact239/240 authored height");
            const auto program=sequence::SequenceProgram::load(snapshot,owner);sequence::SequenceRuntime runtime;
            require(program && runtime.start(*program,1005),"production outgoing root starts at CardPriority");
            engine::SequenceWorld2DSlot native,modern;modern.configureModernIBarSkin(skin);
            for(const auto tick:{0,4,8})
            {
                require(runtime.update(tick).has_value(),"actual outgoing clock advances");
                const auto items=sequence::collectSequenceBitmapRenderData(runtime,snapshot);
                require(items && native.sync(*items,cache) && modern.sync(*items,cache) && native.order()==modern.order(),
                    "actual outgoing leaves publish unchanged production ordering");
                unsigned replaced=0;
                for(const auto node:native.order())
                {
                    const auto* a=native.find(node);const auto* m=modern.find(node);
                    require(a && m && a->clock==m->clock && a->priority==m->priority && a->contentsDataId==m->contentsDataId &&
                        a->worldTransform.values==m->worldTransform.values,"outgoing tint retains every authored clock, priority, contents and full matrix");
                    if(a->contentsDataId==outgoingLeaf){require(a->asset!=m->asset,"moving outgoing face is tinted");verifyFacePixels(*a->asset,*m->asset);++replaced;}
                    else require(a->asset==m->asset,"outgoing1x1 companion retains exact retail pointer");
                }
                require(replaced==1,"exactly one authored outgoing face is recoloured per pose");
                context=false;require(modern.sync(*items,cache).has_value(),"outgoing context fallback publishes");
                for(const auto node:native.order())require(modern.find(node)->asset==native.find(node)->asset,"outgoing context loss returns every retail pointer");
                context=true;
            }
            std::cout<<"[PASS] actual identical Out pair root="<<std::hex<<owner<<" leaf="<<outgoingLeaf<<" idle="<<idleLeaf<<std::dec<<'\n';
        }
        require(identical==31 && textCalls==0,"31 byte-identical outgoing faces preserve authentic ink without any font rasterization");
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
        for(std::size_t p=0;p<original->image.pixels.size();p+=4)
        {original->image.pixels[p]=255;original->image.pixels[p+1]=128;original->image.pixels[p+2]=0;}
        original->image.pixels[0]=original->image.pixels[1]=original->image.pixels[2]=0;
        const auto result=skin.substitute(0x00050029,original);
        require(result!=original && result->image.width==400 && result->image.height==239 &&
            result->dataId==original->dataId,
            "Production St. Charles idle400x239 face retains exact authored footprint");
        verifyFacePixels(*original,*result);
        const auto wrong=bitmap(0x00050983,400,240);
        require(skin.substitute(0x00050029,wrong)==wrong,
            "St. Charles qualification rejects the assumed400x240 footprint");
        context=false;
        require(skin.substitute(0x00050029,original)==original,
            "Presentation context is checked before returning cached239px card");
        context=true;
        require(skin.substitute(0x00050019,bitmap(0x00050815,400,240))->sourceType==data::LegacyDataType::Uap,
            "Neutral-only FaceIn retains its original pixels");
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

    void inspectActionButtons(const std::filesystem::path& root,bool qualify=false)
    {
        const auto paths=data::ResourcePaths::create(std::array{root});data::ResourceRuntime resources;
        require(paths && resources.initialize(*paths).has_value(),"real USA DAT action-button resources open");
        const auto snapshot=resources.snapshot();unsigned nativeFrames=0;
        for(const unsigned index:{1U,3U,5U,6U,12U,17U})for(const unsigned base:{0x008AU,0x0106U})for(unsigned state=0;state<4;++state)
        {
            const auto id=data::packDataId(data::LegacyGroupId::LanguageGraphics,data::DataTag(base+index*4+state));
            auto program=sequence::SequenceProgram::load(snapshot,id,0);require(program.has_value(),"actual action CNK decodes");
            const int rootEnd=(*program)->descriptions().front().record.header.endTime;
            const int lastTick=rootEnd>0 && rootEnd<=512 ? rootEnd : 32;
            for(const auto& description:(*program)->descriptions())
            {
                if(!description.contentsDataId)continue;
                const auto leaf=*description.contentsDataId;
                const auto bytes=snapshot->data().load(leaf);
                if(!bytes)continue;
                const auto metadata=data::inspectLegacyUap(**bytes);
                if(!metadata)continue;
                std::cout<<"action_shape root="<<std::hex<<id<<" payload="<<leaf<<std::dec
                    <<" size="<<metadata->width<<'x'<<metadata->height
                    <<" origin="<<metadata->originX<<','<<metadata->originY<<'\n';
            }
            require(lastTick>=0 && lastTick<=512,"bounded actual action descriptions");
            sequence::SequenceRuntime runtime;require(runtime.start(*program,999).has_value(),"actual action CNK starts");
            bool context=true;unsigned nativeForRoot=0;
            auto skin=std::make_shared<ibar::ModernIBarSkin>(data::LanguageId::EnglishUs,
                [](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>{return std::unexpected("unused");});
            skin->configurePresentationContext([&]{return context;});
            skin->configureActionText([](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>
                {return data::LegacyBitmapRGBA8{30,12,std::vector<std::uint8_t>(30*12*4,128)};});
            skin->configureLayoutProvider([=]{return index==1?ibar::layout::ActionButtonLayout::BuyAuction:ibar::layout::ActionButtonLayout::General;});
            engine::SequenceWorld2DSlot modern;modern.configureModernIBarSkin(skin);
            engine::SequenceWorld2DSlot slot;data::BitmapRuntimeCache cache;
            for(int tick=0;tick<=std::max(12,lastTick)+1;++tick)
            {
                require(runtime.update(tick).has_value(),"actual action clock advances");
                auto items=sequence::collectSequenceBitmapRenderData(runtime,snapshot);require(items && slot.sync(*items,cache),"actual action leaves decode into production slot");
                for(const auto& item:*items)
                {
                    const auto* object=slot.find(item.node);
                    if(qualify)
                    {
                        require(modern.sync(*items,cache).has_value(),"all authored ticks reach modern slot");
                        const auto* changed=modern.find(item.node);
                        require(changed && changed->clock==object->clock && changed->priority==object->priority &&
                            changed->contentsDataId==object->contentsDataId,"native action clock priority identity untouched");
                        if(changed->asset!=object->asset && changed->asset->image.width==object->asset->image.width)
                        {
                            ++nativeFrames;++nativeForRoot;
                            require(changed->worldTransform.values==object->worldTransform.values &&
                                changed->asset->image.height==object->asset->image.height && !changed->asset->presentationRect,
                                "native action retains full original raster pose and geometry");
                            const auto& before=object->asset->image.pixels;const auto& after=changed->asset->image.pixels;
                            for(std::size_t pixel=0;pixel<before.size();pixel+=4)
                            {
                                require(before[pixel+3]==after[pixel+3],"every native action alpha byte retained");
                                const auto low=std::min({before[pixel],before[pixel+1],before[pixel+2]});
                                const auto high=std::max({before[pixel],before[pixel+1],before[pixel+2]});
                                if(!before[pixel+3] || high<=48 || low>=240 || high-low>24)
                                    for(unsigned channel=0;channel<3;++channel) require(before[pixel+channel]==after[pixel+channel],
                                        "original transparent key printed dark ink and highlights retained");
                            }
                            require(skin->substitute(id,object->asset,true,object->worldTransform)==changed->asset,
                                "native transition caches exact source identity");
                            auto wrong=std::make_shared<data::BitmapRuntimeAsset>(*object->asset);wrong->dataId=0x5FFFF;
                            require(skin->substitute(id,wrong,true,object->worldTransform)==wrong,"unknown leaf fails closed");
                            wrong=std::make_shared<data::BitmapRuntimeAsset>(*object->asset);wrong->source.reset();
                            require(skin->substitute(id,wrong,true,object->worldTransform)==wrong,"absent original UAP metadata fails closed");
                            auto singular=object->worldTransform;singular.values[0]=0;singular.values[3]=0;
                            require(skin->substitute(id,object->asset,true,singular)==object->asset,"singular native pose fails closed");

                        }
                        context=false;require(modern.sync(*items,cache).has_value(),"native context fallback sync");
                        for(const auto node:slot.order())require(modern.find(node)->asset==slot.find(node)->asset,
                            "context rejection restores all original source pointers");context=true;
                    }
                    std::cout<<"action_leaf root="<<std::hex<<id<<" payload="<<item.contentsDataId<<std::dec
                        <<" tick="<<tick<<" size="<<object->asset->image.width<<'x'<<object->asset->image.height
                        <<" origin="<<item.metadata.originX<<','<<item.metadata.originY<<" matrix=";
                    for(const auto value:object->worldTransform.values)std::cout<<value<<',';
                    std::cout<<'\n';
                }
            }
            if(qualify && state!=1)require(nativeForRoot>0,"all36 authored enter out pressed roots have native tinted frames");
            if(qualify)std::cout<<"native_root root="<<std::hex<<id<<std::dec<<" tinted_frames="<<nativeForRoot<<'\n';
        }
        if(qualify)require(nativeFrames>0,"production moving native action frames qualified");
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
                    if(m->asset->image.width==a->asset->image.width)
                    {
                        require(m->asset->image.height==a->asset->image.height && m->worldTransform.values==a->worldTransform.values,
                            "new native fallback does not resize or move source");
                        for(std::size_t p=3;p<a->asset->image.pixels.size();p+=4)
                            require(m->asset->image.pixels[p]==a->asset->image.pixels[p],"native fallback preserves full alpha");
                        continue;
                    }
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
    void testAcceptedDeedLineReuse()
    {
        ibar::ModernIBarSkin skin(data::LanguageId::EnglishUs,{});
        ibar::ModernIBarSkin::DeedDescriptor descriptor;
        descriptor.text={{"A B C D",30,20,0,0,12,0,false,false,false}};
        std::vector<std::string> calls;
        skin.configureDeedDescriptors({{0x00050CEA,descriptor}},
            [&](std::string_view text,int size,bool bold,bool italic)->std::expected<data::LegacyBitmapRGBA8,std::string>
            {
                require(size==36 && !bold && !italic,"Line reuse preserves requested font style and size");
                calls.emplace_back(text);
                const unsigned width=unsigned(text.size())*100;
                return data::LegacyBitmapRGBA8{width,12,std::vector<std::uint8_t>(std::size_t(width)*12*4,255)};
            });
        const auto source=bitmap(0x00050CEA,199,227);
        const auto output=skin.substitute(source->dataId,source);
        require(output!=source && calls==std::vector<std::string>{"A","A B","A B C","C","C D"},
            "Overflow renders only the new word, then reuses accepted first and final line bitmaps");
        for(const unsigned y:{90U,101U,102U,113U})
            require(output->image.pixels[(std::size_t(y)*597+63)*4]==0 &&
                output->image.pixels[(std::size_t(y)*597+362)*4]==0,
                "Both accepted line rasters retain width, height and authored left alignment");
        require(skin.substitute(source->dataId,source)==output && calls.size()==5,
            "Cached complete deed invokes no additional wrap callback");
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

    // Independently measured union of actual dat_main phases, root Y-4 applied,
    // clipped inside the original envelope below Roll endingY483 and above nameY530.
    constexpr std::array<std::array<int,4>,11> CurrentPlayerWorldBoxes{{
        {350,492,395,529},{350,501,390,527},{348,491,393,529},
        {350,504,384,528},{347,496,389,526},{345,484,386,528},
        {347,495,396,529},{351,492,393,529},{355,496,387,527},
        {343,496,396,528},{354,484,397,529}}};
    auto currentPlayerPhoto()
    {
        auto photo=std::make_shared<data::LegacyBitmapRGBA8>();
        *photo={768,640,std::vector<std::uint8_t>(768*640*4,0)};
        for(unsigned y=200;y<300;++y)for(unsigned x=100;x<300;++x)
        {const auto i=(std::size_t(y)*768+x)*4;photo->pixels[i]=20;photo->pixels[i+1]=40;
            photo->pixels[i+2]=60;photo->pixels[i+3]=128;}
        return photo;
    }
    void testCurrentPlayerTokenImages()
    {
        // First actual leaf of all eleven CNK_indstra..k, including root StartXY(0,-4).
        constexpr std::array<std::array<unsigned,5>,11> first{{
            {0x1d7,43,23,351,501},{0x1f5,39,17,350,506},{0x302,40,29,352,497},
            {0x213,34,19,350,505},{0x231,37,22,349,501},{0x250,36,44,349,483},
            {0x26d,49,27,347,497},{0x28b,41,25,351,499},{0x2a9,28,31,359,496},
            {0x2c7,49,20,345,500},{0x2e5,39,45,356,484}}};
        bool context=true;unsigned calls=0,last=99;
        ibar::ModernIBarSkin skin(data::LanguageId::EnglishUs,{});
        skin.configurePresentationContext([&]{return context;});
        const auto photo=currentPlayerPhoto();
        skin.configureTokenImages([&](std::uint8_t token){++calls;last=token;return photo;});
        for(unsigned token=0;token<11;++token)
        {
            const auto& f=first[token];const auto& box=CurrentPlayerWorldBoxes[token];
            const auto root=0x2005f+token;
            auto source=bitmap(0x20000+f[0],f[1],f[2]);
            for(std::size_t i=0;i<source->image.pixels.size();++i)source->image.pixels[i]=std::uint8_t(i%251);
            const auto before=source->image.pixels;
            auto matrix=sequence::identity2D();matrix.values[6]=float(f[3]);matrix.values[7]=float(f[4]);
            const auto result=skin.substitute(root,source,true,matrix,258);
            require(result!=source && last==token && result->dataId==source->dataId &&
                result->source==source->source && result->preferLinearFiltering &&
                result->image.width==unsigned(box[2]-box[0])*3 && result->image.height==unsigned(box[3]-box[1])*3 &&
                result->presentationRect==std::optional{std::array<float,4>{float(box[0]),float(box[1]+4),float(box[2]),float(box[3]+4)}},
                "eleven authentic central owners retain fixed thumbnail inside authored union with original identity");
            require(box[1]>=484 && box[3]<530,
                "fixed owner envelope stays below whole Roll/main action band endingY483 and above player nameY530");
            // Actual settled Roll HIT: [361,455]..[463,483]. Check the
            // whole vertical band too, not only horizontal property clearance.
            const bool rollOverlap=box[0]<463 && box[2]>361 && box[1]<483 && box[3]>455;
            require(!rollOverlap,"every fixed central thumbnail clears complete settled Roll HIT rectangle");
            for(int square=0;square<42;++square)
            {
                const auto property=ibar::layout::propertyRect(square);
                if(property.right<=property.left)continue;
                require(box[2]<=property.left || box[0]>=property.right,
                    "fixed central union stays horizontally clear of every property HIT rectangle");
            }
            for(unsigned y=0;y<result->image.height;++y)for(unsigned x=0;x<result->image.width;++x)
                if(x<3 || y<3 || x+3>=result->image.width || y+3>=result->image.height)
                    require(result->image.pixels[(std::size_t(y)*result->image.width+x)*4+3]==0,
                        "fixed central union retains a full logical pixel of transparent edge padding");
            unsigned visible=0,transparent=0;
            for(std::size_t i=0;i<result->image.pixels.size();i+=4)
                if(result->image.pixels[i+3])
                {++visible;require(result->image.pixels[i]==20 && result->image.pixels[i+1]==40 &&
                    result->image.pixels[i+2]==60 && result->image.pixels[i+3]==128,
                    "central preview retains all four RGBA channels including partial alpha");}
                else ++transparent;
            require(visible && transparent && source->image.pixels==before,
                "central preview remains transparent and original complete RGBA immutable");
            const auto previousCalls=calls;
            require(skin.substitute(root,source,true,matrix,258)==result && calls==previousCalls,
                "same authentic central phase reuses bounded derivative cache");
            context=false;require(skin.substitute(root,source,true,matrix,258)==source,
                "central live eligibility is checked before cached artwork");context=true;
            require(skin.substitute(root,source,false,matrix,258)==source &&
                skin.substitute(root,source,true,matrix,257)==source && skin.substitute(root,source)==source,
                "secondary leaf wrong priority and missing placement retain complete native fallback");
            auto wrongMatrix=matrix;wrongMatrix.values[7]+=4;
            require(skin.substitute(root,source,true,wrongMatrix,258)==source,
                "central owner without authentic minusfour root placement remains native");
            wrongMatrix=matrix;wrongMatrix.values[0]=2;
            require(skin.substitute(root,source,true,wrongMatrix,258)==source,
                "unqualified central scale retains native artwork");
            wrongMatrix=matrix;wrongMatrix.values[6]=std::numeric_limits<float>::quiet_NaN();
            require(skin.substitute(root,source,true,wrongMatrix,258)==source,"nonfinite placement fails closed");
            auto wrong=std::make_shared<data::BitmapRuntimeAsset>(*source);wrong->dataId+=0x10000;
            require(skin.substitute(root,wrong,true,matrix,258)==wrong,"wrongbank central leaf cannot share a cached derivative");
            wrong=std::make_shared<data::BitmapRuntimeAsset>(*source);wrong->sourceType=data::LegacyDataType::Bitmap;
            require(skin.substitute(root,wrong,true,matrix,258)==wrong,"central requires actual UAP source type");
            wrong=std::make_shared<data::BitmapRuntimeAsset>(*source);wrong->image.pixels.pop_back();
            require(skin.substitute(root,wrong,true,matrix,258)==wrong,"malformed native RGBA remains native");
            wrong=bitmap(source->dataId,f[1]+1,f[2]);
            require(skin.substitute(root,wrong,true,matrix,258)==wrong,"unmeasured extent rejected before central cache");
            const auto neighbor=token==10?root-1:root+1;
            require(skin.substitute(neighbor,source,true,matrix,258)==source,"leaf belongs only to its exact central root");
        }
        const auto source=bitmap(0x202e5,39,45);auto matrix=sequence::identity2D();matrix.values[6]=356;matrix.values[7]=484;
        skin.configureTokenImages([](std::uint8_t)->std::shared_ptr<const data::LegacyBitmapRGBA8>{return {};});
        require(skin.substitute(0x20069,source,true,matrix,258)==source,"missing central thumbnail preserves complete native asset");
        for(unsigned kind=0;kind<4;++kind)
        {
            auto bad=std::make_shared<data::LegacyBitmapRGBA8>(*photo);
            if(kind==0)bad->width=767;
            if(kind==1)bad->pixels.pop_back();
            if(kind==2)std::fill(bad->pixels.begin(),bad->pixels.end(),0);
            if(kind==3)for(std::size_t i=3;i<bad->pixels.size();i+=4)bad->pixels[i]=255;
            skin.configureTokenImages([bad](std::uint8_t){return bad;});
            require(skin.substitute(0x20069,source,true,matrix,258)==source,
                "wrongsize malformed empty and fullyopaque central preview retains full native RGBA");
        }
        ibar::ModernIBarSkin uk(data::LanguageId::EnglishUk,{}),unqualified(data::LanguageId::EnglishUs,{});
        uk.configurePresentationContext([]{return true;});uk.configureTokenImages([photo](std::uint8_t){return photo;});
        unqualified.configureTokenImages([photo](std::uint8_t){return photo;});
        require(uk.substitute(0x20069,source,true,matrix,258)==source &&
            unqualified.substitute(0x20069,source,true,matrix,258)==source,
            "central thumbnails require EnglishUS and explicit live qualification");
        require(!skin.supports(0x10069) && !skin.supports(0x2005e) && !skin.supports(0x2006a),
            "wrongbank and adjacent roots are not central owners");
    }
    void testActualCurrentPlayerTokens(const std::filesystem::path& root)
    {
        const auto paths=data::ResourcePaths::create(std::array{root});data::ResourceRuntime resources;
        require(paths && resources.initialize(*paths).has_value(),"actual central token DAT opens");
        const auto snapshot=resources.snapshot();std::set<data::DataId> observed;unsigned loops=0;
        for(unsigned token=0;token<11;++token)
        {
            const auto owner=data::packDataId(data::LegacyGroupId::Main,data::DataTag(0x5f+token));
            const auto program=sequence::SequenceProgram::load(snapshot,owner);
            sequence::SequenceRuntime runtime;require(program.has_value(),"actual central CNK loads");
            const auto started=runtime.start(*program,258);require(started.has_value(),"actual central CNK starts");
            require(runtime.moveMatching(owner,258,sequence::moveXYTransform(0,-4))==1 &&
                runtime.setEndingAction(*started,3).has_value(),"authentic central priority placement and loop applied");
            bool context=true;auto skin=std::make_shared<ibar::ModernIBarSkin>(data::LanguageId::EnglishUs,
                ibar::ModernIBarSkin::TextRasterizer{});
            const auto photo=currentPlayerPhoto();skin->configurePresentationContext([&]{return context;});
            skin->configureTokenImages([&](std::uint8_t selected){require(selected==token,"actual central root selects its exact player token");return photo;});
            engine::SequenceWorld2DSlot native,modern;data::BitmapRuntimeCache cache;
            modern.configureModernIBarSkin(skin);
            std::optional<std::int32_t> previousRootClock;
            std::vector<std::uint8_t> ownerPixels;
            const auto& box=CurrentPlayerWorldBoxes[token];
            const std::array<float,4> ownerRect{float(box[0]),float(box[1]+4),float(box[2]),float(box[3]+4)};
            for(int tick=0;tick<=240;tick+=4)
            {
                require(runtime.update(tick).has_value(),"actual central authored loop advances");
                const auto rootState=runtime.inspect(*started);require(rootState.has_value(),"looping central root remains active");
                if(previousRootClock && rootState->clock<*previousRootClock)++loops;
                previousRootClock=rootState->clock;
                const auto items=sequence::collectSequenceBitmapRenderData(runtime,snapshot);
                require(items && !items->empty() && native.sync(*items,cache) && modern.sync(*items,cache),
                    "actual central phase reaches existing bitmap skin seam");
                require(native.order()==modern.order(),"central skin preserves exact native node order");
                for(const auto node:native.order())
                {
                    const auto* a=native.find(node);const auto* m=modern.find(node);
                    require(m && m->asset!=a->asset && m->contentsDataId==a->contentsDataId &&
                        m->clock==a->clock && m->priority==a->priority,"every actual central leaf replaced without changing clock priority identity");
                    require(m->asset->presentationRect==std::optional{ownerRect} &&
                        m->asset->image.width==unsigned(box[2]-box[0])*3 &&
                        m->asset->image.height==unsigned(box[3]-box[1])*3,
                        "all actual phases retain same fixed owner rectangle and raster dimensions");
                    if(ownerPixels.empty())ownerPixels=m->asset->image.pixels;
                    require(m->asset->image.pixels==ownerPixels,
                        "all actual rotating phases produce byteidentical static owner RGBA without size pulsing");
                    require(engine::SequenceWorld2DSlot::transformPoint(m->worldTransform,0,0)==
                            std::array<std::int32_t,2>{box[0],box[1]} &&
                        engine::SequenceWorld2DSlot::transformPoint(m->worldTransform,
                            m->asset->image.width,m->asset->image.height)==
                            std::array<std::int32_t,2>{box[2],box[3]},
                        "all actual phases retain constant world union corners with Roll and name clearance");
                    observed.insert(a->contentsDataId);
                }
                context=false;require(modern.sync(*items,cache).has_value(),"live central context fallback publishes");
                for(const auto node:native.order())
                {
                    const auto* a=native.find(node);const auto* m=modern.find(node);
                    require(m && m->asset==a->asset && m->worldTransform.values==a->worldTransform.values &&
                        m->clock==a->clock && m->priority==a->priority,"context fallback restores complete actual native RGBA and matrix without restarting");
                }
                context=true;
                const auto after=runtime.inspect(*started);
                require(after && after->clock==rootState->clock && after->priority==rootState->priority &&
                    std::get<sequence::Matrix2D>(after->worldTransform).values==
                        std::get<sequence::Matrix2D>(rootState->worldTransform).values,
                    "pixel refresh leaves authentic root clock priority and full matrix unchanged");
            }
        }
        require(observed.size()==328 && loops>=11,"all328 actual central bitmap phases and eleven authentic loops qualified");
        std::cout<<"[PASS] all11 central owners /328 actual UAP phases; fixed static owner pixels/rectangles; authored loops/native fallback retained\n";
    }


    constexpr std::array<unsigned,11> AuctionRoots{4,5,6,10,8,7,3,12,9,13,11};

    void testActualPlayerCashCoin(const std::filesystem::path& root,const std::filesystem::path& output)
    {
        const auto paths=data::ResourcePaths::create(std::array{root});data::ResourceRuntime resources;
        require(paths && resources.initialize(*paths).has_value(),"actual coin DAT opens");
        const auto bytes=resources.snapshot()->data().load(0x20355);require(bool(bytes),"exact retail coin payload loads");
        data::BitmapRuntimeCache cache;const auto original=cache.resolve(0x20355,data::LegacyDataType::Uap,*bytes);
        require(bool(original),"real52x45 cash icon decodes");
        ibar::ModernIBarSkin skin(data::LanguageId::EnglishUs,{});bool context=true;
        skin.configurePresentationContext([&]{return context;});
        const auto first=skin.substitute(0x20355,*original,true,sequence::translate2D(8,254),501);
        require(first!=*original && first->image.width==156 && first->image.height==135 &&
            first->dataId==(*original)->dataId && first->presentationRect==std::optional{std::array<float,4>{0,0,52,45}},
            "real coin gets3x raster with exact52x45 footprint and identity");
        unsigned changed=0;
        for(unsigned y=0;y<135;++y)for(unsigned x=0;x<156;++x)
        {
            const auto a=(std::size_t(y)*156+x)*4,b=(std::size_t(y/3)*52+x/3)*4;
            require(first->image.pixels[a+3]==(*original)->image.pixels[b+3],"coin native alpha mask replicated exactly3x");
            if(y>=93)require(std::equal(first->image.pixels.begin()+a,first->image.pixels.begin()+a+4,
                (*original)->image.pixels.begin()+b),"entire socle31..44 retains pixel-exact straight RGBA");
            else changed+=!std::equal(first->image.pixels.begin()+a,first->image.pixels.begin()+a+3,
                (*original)->image.pixels.begin()+b);
        }
        require(changed>1000,"actual coin colors change materially within allowed upper31 rows");
        for(unsigned count=1;count<=6;++count)for(unsigned column=0;column<count;++column)
            for(unsigned shown:std::array<unsigned,2>{0,column})
        {
            const int width=count>4?130:198,x=int(column)*width+8+3*int(shown);
            const auto world=sequence::translate2D(x,254);
            require(skin.substitute(0x20355,*original,true,world,501)==first,
                "normal and BSSM subset columns share same immutable qualified coin");
            context=false;require(skin.substitute(0x20355,*original,true,world,501)==*original,
                "live qualification loss restores entire original before cached hit");context=true;
        }
        constexpr std::array<std::array<int,2>,4> tradePositions{{{9,395},{609,395},{209,358},{409,358}}};
        for(unsigned icon=0;icon<4;++icon)
        {
            const auto& position=tradePositions[icon];const auto tradeWorld=sequence::translate2D(position[0],position[1]);
            require(skin.substitute(0x20355,*original,true,tradeWorld,100+icon)==first,
                "four authentic Trade cash positions reuse exact qualified coin57 without another raster");
            context=false;require(skin.substitute(0x20355,*original,true,tradeWorld,100+icon)==*original,
                "Trade eligibility loss restores entire native source before shared coin cache");context=true;
            for(unsigned other=0;other<4;++other)if(other!=icon)
                require(skin.substitute(0x20355,*original,true,tradeWorld,100+other)==*original,
                    "Trade cash priority and placement cannot be mixed between owners");
            auto shifted=tradeWorld;shifted.values[6]+=1;
            auto scaled=tradeWorld;scaled.values[0]=.99F;
            require(skin.substitute(0x20355,*original,true,shifted,100+icon)==*original &&
                skin.substitute(0x20355,*original,true,scaled,100+icon)==*original &&
                skin.substitute(0x20355,*original,true,tradeWorld,501)==*original,
                "unmeasured Trade translations scales and Portfolio priority remain exact native fallback");
        }
        const auto world=sequence::translate2D(8,254);
        require(skin.substitute(0x20355,*original,true,world,500)==*original &&
            skin.substitute(0x20355,*original,false,world,501)==*original &&
            skin.substitute(0x20355,*original,true,sequence::translate2D(9,254),501)==*original &&
            skin.substitute(0x20355,*original,true,sequence::translate2D(8,255),501)==*original,
            "coin priority principal and exact authored translation are required");
        for(unsigned mode=0;mode<5;++mode)
        {
            auto bad=std::make_shared<data::BitmapRuntimeAsset>(**original);
            if(mode==0)bad->source.reset();
            if(mode==1)bad->sourceType=data::LegacyDataType::Native;
            if(mode==2)bad->dataId=0x30355;
            if(mode==3)bad->image.pixels[100]^=1;
            if(mode==4){auto payload=std::make_shared<data::DataBytes>(*bad->source);(*payload)[100]^=std::byte{1};bad->source=payload;}
            require(skin.substitute(0x20355,bad,true,world,501)==bad,
                "arbitrary header-compatible or mutated source/RGBA retains full native fallback");
            require(skin.substitute(0x20355,bad,true,sequence::translate2D(9,395),100)==bad,
                "Trade also rejects every arbitrary or mutated source/RGBA");
        }
        const auto program=sequence::SequenceProgram::load(resources.snapshot(),0x20355);
        sequence::SequenceRuntime runtime;require(program && runtime.start(*program,501),"actual raw coin root starts");
        require(runtime.moveMatching(0x20355,501,sequence::moveXYTransform(8,254))==1 && runtime.update(60),
            "actual raw coin advances unchanged native clock");
        const auto items=sequence::collectSequenceBitmapRenderData(runtime,resources.snapshot());
        engine::SequenceWorld2DSlot native,modern;modern.configureModernIBarSkin(std::make_shared<ibar::ModernIBarSkin>(skin));
        require(items && native.sync(*items,cache) && modern.sync(*items,cache),"real coin reaches existing slot seam");
        for(const auto node:native.order())
        {
            const auto* a=native.find(node);const auto* m=modern.find(node);
            require(m && m->clock==a->clock && m->priority==a->priority && m->contentsDataId==a->contentsDataId &&
                engine::SequenceWorld2DSlot::transformPoint(m->worldTransform,156,135)==std::array<std::int32_t,2>{60,299},
                "raw coin clocks priority and full authored52x45 world footprint unchanged");
        }
        std::filesystem::create_directories(output);
        const auto write=[&](const char* name,const data::LegacyBitmapRGBA8& image)
        {
            std::ofstream file(output/name,std::ios::binary);
            file.write(reinterpret_cast<const char*>(image.pixels.data()),std::streamsize(image.pixels.size()));
            require(bool(file),"actual coin raw RGBA CPU preview saved");
        };
        write("coin-before-52x45.rgba",(*original)->image);write("coin-after-156x135.rgba",first->image);
        std::cout<<"[PASS] exact retail coin/source/RGBA qualification,3x palette, native alpha/socle/clock/geometry and fallback\n";
    }

    void testAuctionTokenImages()
    {
        constexpr std::array<std::array<int,5>,11> shapes{{
            {0xb0,42,22,-19,-19},{0xc8,38,15,-18,-13},{0xda,29,21,-13,-18},
            {0x12c,31,13,-15,-11},{0xfa,28,14,-13,-11},{0xe9,20,24,-9,-23},
            {0xa0,34,19,-16,-18},{0x14c,27,16,-13,-15},{0x113,17,19,-8,-18},
            {0x15d,41,15,-24,-12},{0x13d,20,19,-9,-17}}};
        ibar::ModernIBarSkin skin(data::LanguageId::EnglishUs,{});
        bool context=true;skin.configurePresentationContext([&]{return context;});
        auto photo=currentPlayerPhoto();unsigned selected=99;
        skin.configureTokenImages([&](std::uint8_t token){selected=token;return photo;});
        for(unsigned token=0;token<11;++token)for(unsigned count=1;count<=6;++count)
            for(unsigned player=0;player<count;++player)
        {
            const auto& shape=shapes[token];const auto owner=0x30000+AuctionRoots[token];
            const auto source=bitmap(0x30000+shape[0],shape[1],shape[2]);
            const int width=count>4?134:201,spacing=(800-int(count)*width)/int(count+1);
            const int center=spacing+int(player)*(width+spacing)+width/2;
            const auto world=sequence::translate2D(center,560);
            const auto raster=sequence::translate2D(center+shape[3],560+shape[4]);
            const auto modern=skin.substitute(owner,source,true,raster,316+player,world);
            require(modern!=source && selected==token && modern->dataId==source->dataId &&
                modern->preferLinearFiltering && modern->image.width==162 && modern->image.height==45 &&
                modern->presentationRect==std::optional{std::array<float,4>{-27,-10,27,5}},
                "all eleven auction owners and every native1..6 player column use fixed54x15 canvas");
            for(unsigned y=0;y<45;++y)for(unsigned x=0;x<162;++x)
            {
                const auto at=(std::size_t(y)*162+x)*4;
                if(modern->image.pixels[at+3])require(x>=3 && x<159 && y>=3 && y<42 &&
                    modern->image.pixels[at]==20 && modern->image.pixels[at+1]==40 &&
                    modern->image.pixels[at+2]==60 && modern->image.pixels[at+3]==128,
                    "auction photo preserves RGB partial alpha and one logical pixel transparent padding");
            }
            require(skin.substitute(owner,source,true,raster,316+player,world)==modern,
                "qualified auction placement reuses immutable pixels");
            context=false;require(skin.substitute(owner,source,true,raster,316+player,world)==source,
                "live auction context loss restores full native asset before cached hit");context=true;
            require(skin.substitute(owner,source,true,raster,{},world)==source &&
                skin.substitute(owner,source,true,raster,315,world)==source &&
                skin.substitute(owner,source,true,raster,316+player,{})==source &&
                skin.substitute(owner,source,false,raster,316+player,world)==source,
                "auction needs authentic root priority transform and principal leaf");
            auto shifted=raster;shifted.values[6]+=1;
            require(skin.substitute(owner,source,true,shifted,316+player,world)==source,
                "unmeasured auction leaf bbox fails completely");
            auto badWorld=world;badWorld.values[7]+=1;
            require(skin.substitute(owner,source,true,raster,316+player,badWorld)==source,
                "unmeasured auction root placement fails completely");
            const auto wrong=bitmap(source->dataId,shape[1]+1,shape[2]);
            require(skin.substitute(owner,wrong,true,raster,316+player,world)==wrong,
                "auction extent mismatch retains native full RGBA");
        }
        const auto source=bitmap(0x300b0,42,22);const auto world=sequence::translate2D(67,560);
        const auto raster=sequence::translate2D(48,541);
        require(skin.substitute(0x30004,source,true,raster,317,world)==source,
            "auction column must match priority player rather than merely any column");
        for(unsigned mode=0;mode<4;++mode)
        {
            auto bad=std::make_shared<data::LegacyBitmapRGBA8>(*photo);
            if(mode==0)bad->pixels.pop_back();if(mode==1)bad->width=767;
            if(mode==2)std::fill(bad->pixels.begin(),bad->pixels.end(),0);
            if(mode==3)for(std::size_t at=3;at<bad->pixels.size();at+=4)bad->pixels[at]=255;
            skin.configureTokenImages([bad](std::uint8_t){return bad;});
            require(skin.substitute(0x30004,source,true,raster,316,world)==source,
                "missing invalid or alpha-incompatible auction PNG preserves entire native bitmap");
        }
    }

    void testActualAuctionTokens(const std::filesystem::path& root)
    {
        const auto paths=data::ResourcePaths::create(std::array{root});data::ResourceRuntime resources;
        require(paths && resources.initialize(*paths).has_value(),"actual auction DAT opens");
        const auto snapshot=resources.snapshot();std::set<std::pair<data::DataId,std::size_t>> phases;
        unsigned leavesObserved=0;
        for(unsigned token=0;token<11;++token)
        {
            const auto owner=0x30000+AuctionRoots[token];
            const auto program=sequence::SequenceProgram::load(snapshot,owner);
            require(program.has_value(),"actual auction CNK loads");sequence::SequenceRuntime runtime;
            const auto started=runtime.start(*program,316);require(started.has_value(),"actual auction root starts");
            require(runtime.moveMatching(owner,316,sequence::moveXYTransform(67,560))==1,
                "authentic six-player auction placement applied without ending-action override");
            bool context=true;auto skin=std::make_shared<ibar::ModernIBarSkin>(data::LanguageId::EnglishUs,
                ibar::ModernIBarSkin::TextRasterizer{});auto photo=currentPlayerPhoto();
            skin->configurePresentationContext([&]{return context;});
            skin->configureTokenImages([&](std::uint8_t selected){require(selected==token,
                "actual auction root selects exact token");return photo;});
            engine::SequenceWorld2DSlot native,modern;modern.configureModernIBarSkin(skin);
            data::BitmapRuntimeCache cache;std::vector<std::uint8_t> fixedPixels;
            for(int tick=0;tick<=120;++tick)
            {
                require(runtime.update(tick).has_value(),"actual auction native clock advances");
                const auto items=sequence::collectSequenceBitmapRenderData(runtime,snapshot);
                require(items && native.sync(*items,cache) && modern.sync(*items,cache),
                    "auction authentic phases pass production bitmap seam");
                require(native.order()==modern.order(),"auction node traversal unchanged");
                for(const auto node:native.order())
                {
                    const auto* a=native.find(node);const auto* m=modern.find(node);
                    const auto state=runtime.inspect(node);require(state.has_value(),"actual auction leaf exists");
                    phases.emplace(owner,state->offset);++leavesObserved;
                    require(m && m->asset!=a->asset && m->clock==a->clock && m->priority==a->priority &&
                        m->contentsDataId==a->contentsDataId && m->asset->image.width==162 && m->asset->image.height==45,
                        "actual auction phase pixels modernize with unchanged clock priority identity");
                    require(engine::SequenceWorld2DSlot::transformPoint(m->worldTransform,0,0)==
                        std::array<std::int32_t,2>{40,550} &&
                        engine::SequenceWorld2DSlot::transformPoint(m->worldTransform,162,45)==
                        std::array<std::int32_t,2>{94,565},"every auction phase clears name and cash surfaces");
                    if(fixedPixels.empty())fixedPixels=m->asset->image.pixels;
                    require(m->asset->image.pixels==fixedPixels,"all authored auction phases keep static photo size");
                }
                context=false;require(modern.sync(*items,cache).has_value(),"auction live fallback sync succeeds");
                for(const auto node:native.order())
                {
                    const auto* a=native.find(node);const auto* m=modern.find(node);
                    require(m && m->asset==a->asset && m->worldTransform.values==a->worldTransform.values &&
                        m->clock==a->clock && m->priority==a->priority,"auction fallback preserves full native RGBA matrix clock priority");
                }
                context=true;
            }
        }
        require(phases.size()==196 && leavesObserved>=196,"all196 authored auction record phases observed independently");
        std::cout<<"[PASS] all11 auction owners /196 authored phases; fixed54x15 pixels, native clocks and full fallback\n";
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
    try{if(argc==4 && std::string_view(argv[1])=="--player-cash-coin-qualify"){testActualPlayerCashCoin(argv[2],argv[3]);return 0;}if(argc==3 && std::string_view(argv[1])=="--auction-token-qualify"){testActualAuctionTokens(argv[2]);return 0;}if(argc==4 && std::string_view(argv[1])=="--readable-card-qualify"){testActualLoanFooter(argv[2],argv[3],true);return 0;}if(argc==4 && std::string_view(argv[1])=="--loan-card-qualify"){testActualLoanFooter(argv[2],argv[3]);return 0;}if(argc==3 && std::string_view(argv[1])=="--idle-card-trade-qualify"){testActualIdleCardTrade(argv[2]);return 0;}if(argc==3 && std::string_view(argv[1])=="--current-token-qualify"){testActualCurrentPlayerTokens(argv[2]);return 0;}if(argc==3 && std::string_view(argv[1])=="--out-card-qualify"){testActualNativeOutCards(argv[2]);return 0;}if(argc==4 && std::string_view(argv[1])=="--deed-benchmark"){benchmarkActualDeeds(argv[2],argv[3]);return 0;}if(argc==3 && std::string_view(argv[1])=="--idle-card-qualify"){testActualNativeIdleCards(argv[2]);return 0;}if(argc==3 && std::string_view(argv[1])=="--chance-idle-qualify"){testActualNativeIdleCards(argv[2],true);return 0;}if(argc==3 && std::string_view(argv[1])=="--card-face-inspect"){testActualCardFaces(argv[2],true);return 0;}if(argc==3 && std::string_view(argv[1])=="--card-face-qualify"){testActualCardFaces(argv[2]);return 0;}if(argc==3 && std::string_view(argv[1])=="--action-qualify"){testActualActionButtons(argv[2]);return 0;}if(argc==3 && std::string_view(argv[1])=="--native-action-qualify"){inspectActionButtons(argv[2],true);return 0;}if(argc==3 && std::string_view(argv[1])=="--action-inspect"){inspectActionButtons(argv[2]);return 0;}testCardFaceIn();testChanceNativeIdle();testAllNativeIdleSources();testMeasuredStCharlesIdleCard();testPortfolioMiniatures();testAcceptedDeedLineReuse();testMeasuredDeedArtwork();testMeasuredRetailTrade();testMeasuredNavigationAA();testScoreTokenImages();testCurrentPlayerTokenImages();testAuctionTokenImages();testPurchaseDeedPlacement();std::cout<<"[PASS] measured Trade footprint and context fallback\n";return 0;}
    catch(const std::exception& e){std::cerr<<"[FAIL] "<<e.what()<<'\n';return 1;}
}
