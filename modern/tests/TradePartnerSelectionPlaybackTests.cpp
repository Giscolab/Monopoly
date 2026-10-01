#include "TradePartnerSelectionPlayback.hpp"
#include "SyntheticTextResources.hpp"
#include <iostream>
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
    try{if(argc==3 && std::string_view(argv[1])=="--retail-qualify"){qualifyRetail(argv[2]);return 0;}testMissingResources();testChooser();std::cout<<"[PASS] localized Trade partner consumer and authentic input/font/lifetime (CPU)\n";return 0;}
    catch(const std::exception& error){std::cerr<<"[FAIL] "<<error.what()<<'\n';return 1;}
}
