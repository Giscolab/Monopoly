#include "TradePartnerSelectionPlayback.hpp"
#include "SyntheticTextResources.hpp"
#include "ModernTokenPreview.hpp"
#include <iostream>
#include <algorithm>
#include <stdexcept>
namespace
{
    using namespace monopoly;
    void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    struct Fixture
    {
        SyntheticTextResources resources{{{2015,u"Localized partner title"}},
            {{0x01C0,{data::LegacyDataType::Bitmap,SyntheticSequenceResources::bitmap24()}},
             {0x01C1,{data::LegacyDataType::Bitmap,SyntheticSequenceResources::bitmap24()}},
             {0x01C2,{data::LegacyDataType::Bitmap,SyntheticSequenceResources::bitmap24()}}}};
        fonts::Runtime font;
        Fixture()
        {
            resources.service.shutdown();
            data::DataBytes bytes(32+188*209,std::byte{1});
            const auto put=[&](unsigned at,unsigned value,unsigned size)
            {for(unsigned i=0;i<size;++i)bytes[at+i]=std::byte((value>>(8*i))&255);};
            for(unsigned i=0;i<32;++i)bytes[i]=std::byte{0};
            put(0,188,2);put(2,209,2);put(8,2,4);put(12,2,2);put(14,2,2);
            put(24,0x606060,4);put(28,111,4);
            std::vector<data::ArchiveBuildItem> items(0x0590);
            items[0x058F]={data::LegacyDataType::Uap,std::move(bytes)};
            require(data::writeLegacyDataArchive(resources.directory/"Dat_Mon/dat_pat.dat",items).has_value(),"Synthetic explicit chooser template");
            const auto paths=data::ResourcePaths::create(std::array{resources.directory});
            require(paths && resources.service.initialize(*paths).has_value(),"Chooser resource registry");
            loadRealTestArial(font); require(font.saveSettings(0).has_value(),"Save original font slot");
        }
    };
    rules::GameState game()
    {
        rules::GameState game;game.numberOfPlayers=6;
        for(unsigned p=0;p<6;++p){game.players[p].name=L"Player "+std::to_wstring(p+1);game.players[p].token=p%3;game.players[p].currentSquare=p;}
        game.players[1].currentSquare=tradeui::OffBoardSquare;game.players[2].token=0;return game;
    }
    void qualifyRetail(const std::filesystem::path& root)
    {
        const auto paths=data::ResourcePaths::create(std::array{root});data::ResourceRuntime resources;
        require(paths && resources.initialize(*paths).has_value(),"Actual chooser DAT loads");
        fonts::Runtime font;loadRealTestArial(font);engine::SequencePlayback playback(resources.snapshot());
        auto players=game();players.players[1].currentSquare=1;
        data::BitmapRuntimeCache decoded;
        for(unsigned token=0;token<11;++token)
        {
            const auto id=0x000201C0+token;
            const auto metadata=resources.snapshot()->data().metadata(id);
            const auto bytes=resources.snapshot()->data().load(id);
            require(metadata && bytes,"Actual token mini metadata and bytes");
            const auto asset=decoded.resolve(id,metadata->type,*bytes);require(asset.has_value(),"Actual token mini decodes");
            int originX=0,originY=0;
            if(metadata->type==data::LegacyDataType::Uap)
            {const auto uap=data::inspectLegacyUap(**bytes);require(uap.has_value(),"Actual mini UAP metadata");originX=uap->originX;originY=uap->originY;}
            std::cout<<"[retail-mini] token="<<token<<" id="<<std::hex<<id<<std::dec<<" width="<<(*asset)->image.width
                <<" height="<<(*asset)->image.height<<" origin="<<originX<<','<<originY<<" type="<<int(metadata->type)<<'\n';
        }
        tradeui::PartnerSelectionPlayback panel;unsigned count=0;
        for(unsigned first=0;first<15;first+=5)
        {
            for(unsigned p=0;p<5;++p)players.players[p].token=(first+p)%11;
            tradeui::State state;require(tradeui::beginLocalTrade(state,players,5),"Actual resource chooser state");
            for(const bool modern:{false,true})
            {
                require(panel.sync(state,players,display::Screen2D::Trade,&font,playback,modern).has_value() &&
                    playback.update(0).has_value(),"Actual LANG2015/PAT3058F/Main11tokens complete chooser raster");
                ++count;
            }
        }
        require(count==6 && panel.reset(playback).has_value(),"Six actual native/modern raster sets acrossall11 retail tokens");
        std::cout<<"[PASS] actual localized chooser template/title/all11 token CPU decode (not live gameplay/GPU)\n";
    }
    void qualifyThumbnails(const std::filesystem::path& root,const std::filesystem::path& thumbnails)
    {
        const auto paths=data::ResourcePaths::create(std::array{root});data::ResourceRuntime resources;
        require(paths && resources.initialize(*paths).has_value(),"Actual thumbnail qualification DAT");
        fonts::Runtime font;loadRealTestArial(font);engine::SequencePlayback playback(resources.snapshot());
        menu::ModernTokenPreview loader(thumbnails);auto players=game();players.players[1].currentSquare=1;
        tradeui::State state;require(tradeui::beginLocalTrade(state,players,5),"Actual thumbnail chooser state");
        tradeui::PartnerSelectionPlayback panel;
        for(unsigned token=0;token<11;++token)
        {
            players.players[0].token=token;
            require(panel.sync(state,players,display::Screen2D::Trade,&font,playback,true).has_value(),"Actual retail mini fallback");
            const auto retail=playback.runtimeBitmaps().asset(panel.surface());
            const auto thumbnail=loader.image(static_cast<std::uint8_t>(token),255);
            require(thumbnail && thumbnail->width==768 && thumbnail->height==640,"Actual shared GPU thumbnail decoded");
            panel.configureTokenImages([&loader](std::uint8_t id){return loader.image(id,255);});
            require(panel.sync(state,players,display::Screen2D::Trade,&font,playback,true).has_value() && playback.update(0).has_value(),
                "Actual thumbnail composite retains original chooser sequence");
            const auto modern=playback.runtimeBitmaps().asset(panel.surface());
            require(modern && modern->image.width==564 && modern->image.height==627 && modern->image.pixels!=retail->image.pixels,
                "Actual shared token artwork differs from retail mini and fits owned chooser raster");
            std::cout<<"[thumbnail] token="<<token<<" source=768x640 composite=564x627 well=132x84\n";
            panel.configureTokenImages({});
        }
        require(panel.reset(playback).has_value(),"Actual thumbnail qualification releases owned chooser");
        std::cout<<"[PASS] all11 actual shared thumbnails in localized Trade chooser (CPU only)\n";
    }
    void testThumbnails()
    {
        Fixture fixture;engine::SequencePlayback playback(fixture.resources.service.snapshot());
        auto players=game();tradeui::State state;require(tradeui::beginLocalTrade(state,players,5),"Thumbnail chooser state");
        tradeui::PartnerSelectionPlayback panel;
        const auto sync=[&](bool modern){return panel.sync(state,players,display::Screen2D::Trade,&fixture.font,playback,modern);};
        require(sync(false).has_value(),"Native thumbnail baseline");const auto native=playback.runtimeBitmaps().asset(panel.surface());
        require(sync(true).has_value() && playback.update(60).has_value(),"Modern retail mini baseline");
        const auto fallback=playback.runtimeBitmaps().asset(panel.surface());const auto nodes=playback.world2D().order();
        const auto before=*playback.world2D().find(nodes[0]);
        auto image=std::make_shared<data::LegacyBitmapRGBA8>(data::LegacyBitmapRGBA8{768,640,std::vector<std::uint8_t>(768*640*4,0)});
        for(unsigned y=20;y<40;++y)for(unsigned x=10;x<50;++x)
        {const auto p=(std::size_t(y)*768+x)*4;image->pixels[p]=200;image->pixels[p+1]=20;image->pixels[p+2]=10;image->pixels[p+3]=128;}
        unsigned calls=0;panel.configureTokenImages([&](std::uint8_t){++calls;return image;});
        require(sync(true).has_value() && playback.update(60).has_value(),"Valid transparent thumbnail replaces retail mini");
        const auto modern=playback.runtimeBitmaps().asset(panel.surface());
        const auto pixel=[&](unsigned x,unsigned y,unsigned c){return modern->image.pixels[(std::size_t(y)*564+x)*4+c];};
        const unsigned firstTop=(261-234)*3;
        require(pixel(378,firstTop+8,0)==22 && pixel(378,firstTop+9,0)==std::uint8_t((200*128+22*127+127)/255) &&
            pixel(509,firstTop+74,0)==pixel(378,firstTop+9,0) && pixel(378,firstTop+75,0)==22,
            "Visible alpha crop keeps2:1aspect in132x84well, centered and source-over blended without stretching");
        const auto after=*playback.world2D().find(nodes[0]);
        require(after.clock==before.clock && after.priority==before.priority && after.worldTransform.values==before.worldTransform.values,
            "Modern thumbnail refresh retains chooser geometry priority and clock");
        const auto once=calls;require(sync(true).has_value() && calls==once && playback.runtimeBitmaps().asset(panel.surface())==modern,
            "Unchanged chooser does not request thumbnail or rerasterize");
        require(sync(false).has_value() && calls==once && playback.runtimeBitmaps().asset(panel.surface())->image.pixels==native->image.pixels,
            "Native mode never requests provider and restores exact original panel RGBA");
        const auto checkFallback=[&](tradeui::PartnerSelectionPlayback::TokenImageProvider provider)
        {panel.configureTokenImages(std::move(provider));require(sync(true).has_value() &&
            playback.runtimeBitmaps().asset(panel.surface())->image.pixels==fallback->image.pixels,"Malformed/missing thumbnail keeps exact decoded retail mini fallback");};
        checkFallback({});checkFallback([](std::uint8_t){return std::shared_ptr<const data::LegacyBitmapRGBA8>{};});
        auto bad=std::make_shared<data::LegacyBitmapRGBA8>(*image);bad->width=767;checkFallback([bad](std::uint8_t){return bad;});
        bad=std::make_shared<data::LegacyBitmapRGBA8>(*image);bad->pixels.pop_back();checkFallback([bad](std::uint8_t){return bad;});
        bad=std::make_shared<data::LegacyBitmapRGBA8>(*image);std::fill(bad->pixels.begin(),bad->pixels.end(),0);checkFallback([bad](std::uint8_t){return bad;});
        bad=std::make_shared<data::LegacyBitmapRGBA8>(*image);for(std::size_t p=3;p<bad->pixels.size();p+=4)bad->pixels[p]=255;checkFallback([bad](std::uint8_t){return bad;});
        checkFallback([](std::uint8_t)->std::shared_ptr<const data::LegacyBitmapRGBA8>{throw std::runtime_error("Optional provider failure");});
        require(playback.runtimeBitmaps().size()==1 && panel.reset(playback).has_value(),"Thumbnail provider never allocates additional runtime surfaces");
    }
    void testMissingResources()
    {
        SyntheticTextResources resources{{{2015,u"Localized partner title"}}};
        fonts::Runtime font;loadRealTestArial(font);engine::SequencePlayback playback(resources.service.snapshot());
        auto players=game();tradeui::State state;require(tradeui::beginLocalTrade(state,players,5),"Missing-template fixture state");
        tradeui::PartnerSelectionPlayback panel;
        require(!panel.sync(state,players,display::Screen2D::Trade,&font,playback,true) &&
            !panel.visible() && panel.surface()==data::EmptyDataId && playback.runtimeBitmaps().size()==0,
            "Missing original chooser template reports failure instead of inventing blank or replacement data");
    }
    void testChooser()
    {
        Fixture fixture;engine::SequencePlayback playback(fixture.resources.service.snapshot());
        auto players=game();tradeui::State state;
        require(tradeui::beginLocalTrade(state,players,5),"Human six enters authentic partner selection");
        tradeui::PartnerSelectionPlayback panel;const auto settings=fixture.font.settings();
        const auto originalSlot=*fixture.font.savedSettings(0);
        require(panel.sync(state,players,display::Screen2D::Trade,&fixture.font,playback).has_value() &&
            panel.visible() && playback.update(0).has_value(),"Missing native chooser restored through real bitmap slot");
        const auto id=panel.surface();const auto native=playback.runtimeBitmaps().asset(id);
        require(native && native->image.width==188 && native->image.height==209 &&
            !native->preferLinearFiltering && native->image.pixels[3]==111,
            "Unknown/non-opt-in context uses actual native template RGBA and fonts");
        const auto nodes=playback.world2D().order();require(nodes.size()==1,"Single chooser root");
        const auto before=*playback.world2D().find(nodes[0]);
        require(before.priority==205 && before.worldTransform.values[6]==306 && before.worldTransform.values[7]==234,
            "Chooser retains retail priority and logical hit geometry");
        require(panel.sync(state,players,display::Screen2D::Trade,&fixture.font,playback,true).has_value() &&
            playback.update(0).has_value(),"Strict USA/en-US opt-in modernizes only pixels");
        const auto modern=playback.runtimeBitmaps().asset(id);const auto after=*playback.world2D().find(nodes[0]);
        require(modern!=native && modern->image.width==564 && modern->image.height==627 && modern->preferLinearFiltering &&
            modern->presentationRect==std::optional<std::array<float,4>>{{0,0,188,209}} && after.clock==before.clock &&
            after.priority==before.priority && after.worldTransform.values[6]==before.worldTransform.values[6] &&
            after.worldTransform.values[7]==before.worldTransform.values[7] &&
            after.worldTransform.values[0]*3==before.worldTransform.values[0] &&
            after.worldTransform.values[4]*3==before.worldTransform.values[4],
            "Supersampled chooser keeps original node, clock, world placement and footprint");
        require(fixture.font.settings()==settings && *fixture.font.savedSettings(0)==originalSlot,
            "Chooser private font leaves caller settings and slots intact");
        require(panel.sync(state,players,display::Screen2D::Trade,&fixture.font,playback,true).has_value() &&
            playback.runtimeBitmaps().asset(id)==modern,"Stable state reuses immutable bitmap without raster churn");
        require(!tradeui::playerTokenRect(state,players,1) && !tradeui::playerTokenRect(state,players,5) &&
            *tradeui::playerTokenRect(state,players,2)==tradeui::Rect{432,293,485,322},
            "Inactive/source rows absent; eligible rows retain original PLAYER numbers and compact hit positions");
        uimsg::Message key;key.type=uimsg::Type::TextInput;key.text="2";
        require(!tradeui::planPartnerSelection(state,players,display::Screen2D::Trade,key),"Inactive player number not selectable");
        key.text="3";const auto partner=tradeui::planPartnerSelection(state,players,display::Screen2D::Trade,key);
        require(partner && *partner==2,"TextInput3 selects player index2, independently of displayed token index0");
        uimsg::Message click;click.type=uimsg::Type::MouseLeftDown;click.numberA=459;click.numberB=309;
        require(!tradeui::planPartnerSelection(state,players,display::Screen2D::Trade,click),"Original entry click guard preserved");
        const auto mouse=tradeui::planPartnerSelection(state,players,display::Screen2D::Trade,click);
        require(mouse && *mouse==2 && tradeui::selectPartner(state,players,*mouse),"Second native row click selects same partner");
        require(panel.sync(state,players,display::Screen2D::Trade,&fixture.font,playback,true).has_value() &&
            !panel.visible() && playback.update(0).has_value() && playback.world2D().order().empty(),"Selection retires only chooser display");
        require(tradeui::beginLocalTrade(state,players,5),"Reopen partner chooser");
        require(!panel.sync(state,players,display::Screen2D::Trade,nullptr,playback,true) && !panel.visible(),
            "Missing font reports explicit failure without blank visible panel");
        players.players[2].name=L"Renamed partner";
        require(panel.sync(state,players,display::Screen2D::Trade,&fixture.font,playback,true).has_value() &&
            panel.surface()==id && playback.runtimeBitmaps().asset(id)!=modern,"Dynamic name invalidates content, retains bounded surface");
        require(panel.reset(playback).has_value() && playback.runtimeBitmaps().size()==0 &&
            playback.update(0).has_value(),"Reset releases owned surface and root");
    }
}
int main(int argc,char** argv)
{
    try{if(argc==4 && std::string_view(argv[1])=="--thumbnail-qualify"){qualifyThumbnails(argv[2],argv[3]);return 0;}if(argc==3 && std::string_view(argv[1])=="--retail-qualify"){qualifyRetail(argv[2]);return 0;}testMissingResources();testChooser();testThumbnails();std::cout<<"[PASS] localized Trade partner consumer and authentic input/font/lifetime (CPU)\n";return 0;}
    catch(const std::exception& error){std::cerr<<"[FAIL] "<<error.what()<<'\n';return 1;}
}
