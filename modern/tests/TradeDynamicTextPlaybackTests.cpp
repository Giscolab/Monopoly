#include "TradeNamePlayback.hpp"
#include "TradePanelTextPlayback.hpp"
#include "TradeCashTextPlayback.hpp"
#include "SyntheticTextResources.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace monopoly;
namespace
{
    void require(bool value, std::string_view message)
    {
        if (!value) throw std::runtime_error(std::string(message));
    }
    struct Fixture
    {
        SyntheticTextResources resources{{{2013,u"Trading description heading"},
            {2014,u"Several trading instructions wrap across native measured lines without changing the authoritative panel layout."}},
            {{tradeui::TradeCashIconTag,{data::LegacyDataType::Chunky,
                SyntheticSequenceResources::words({0x03000014,0,0x04000064,2,0x00000356})}},
             {0x0356,{data::LegacyDataType::Bitmap,SyntheticSequenceResources::bitmap24()}}}};
        fonts::Runtime font;
        rules::GameState game;
        tradeui::State state;
        Fixture()
        {
            loadRealTestArial(font);
            require(font.saveSettings(0).has_value(),"save retail default font slot");
            require(font.setSize(8).has_value(),"set cash title font");
            font.setWeight(500);
            require(font.saveSettings(8).has_value() && font.restoreSettings(0).has_value(),"save cash font and restore default");
            game.numberOfPlayers=2; game.players[0].name=L"Alice coverage"; game.players[1].name=L"Bob";
            state.playerA=0; state.playerB=1; state.cashDesired={1234,5678,90,12};
        }
    };
    template<class Sync> void qualifyModes(Fixture& fixture, engine::SequencePlayback& playback, Sync sync)
    {
        require(sync(false).has_value() && playback.update(0).has_value(),"native dynamic Trade text publishes");
        const auto nodes=playback.world2D().order();
        std::vector<engine::SequenceWorld2DObject> native;
        for(const auto node:nodes) native.push_back(*playback.world2D().find(node));
        // Raw bitmap roots use timeMultiple60; the cash icon retains its own
        // authored clock quantum. Sample both after a complete native quantum,
        // before requesting a redraw at the same parent tick.
        require(playback.update(60).has_value(),"advance original sequence clocks");
        std::vector<std::int32_t> runningClocks;
        for(const auto node:nodes)
        {
            const auto& object=*playback.world2D().find(node);
            runningClocks.push_back(object.clock);
            if(data::isRuntimeBitmapDataId(object.contentsDataId))
                require(object.clock==60,"raw text bitmap retains its native sixty-tick clock quantum");
        }
        const auto caller=fixture.font.settings();
        require(sync(true).has_value() && fixture.font.settings()==caller && playback.update(60).has_value(),"modern glyph pass restores caller font");
        require(playback.world2D().order()==nodes,"modern pixel refresh retains all original nodes");
        bool coverage=false;
        std::vector<std::shared_ptr<const data::BitmapRuntimeAsset>> modernAssets;
        for(std::size_t i=0;i<nodes.size();++i)
        {
            const auto& modern=*playback.world2D().find(nodes[i]);
            require(modern.priority==native[i].priority && modern.contentsDataId==native[i].contentsDataId && modern.clock==runningClocks[i],"mode change preserves priority id and running clock");
            require(modern.worldTransform.values[6]==native[i].worldTransform.values[6] && modern.worldTransform.values[7]==native[i].worldTransform.values[7],"mode change preserves authoritative placement");
            modernAssets.push_back(modern.asset);
            if(!data::isRuntimeBitmapDataId(modern.contentsDataId))
            {
                require(modern.asset==native[i].asset,"cash icon remains original");
                continue;
            }
            const auto w=native[i].asset->image.width,h=native[i].asset->image.height;
            require(modern.asset->image.width==w*3 && modern.asset->image.height==h*3 && modern.asset->preferLinearFiltering &&
                modern.asset->presentationRect==std::optional<std::array<float,4>>{{0,0,float(w),float(h)}},"three times glyph raster retains native logical footprint");
            for(std::size_t p=3;p<modern.asset->image.pixels.size();p+=4)
                coverage|=modern.asset->image.pixels[p]>0 && modern.asset->image.pixels[p]<255;
        }
        require(coverage,"actual modern glyph coverage includes transparent antialiased edges");
        require(sync(true).has_value() && playback.commands().pendingCount()==0,"unchanged modern content emits no commands");
        for(std::size_t i=0;i<nodes.size();++i)
            require(playback.world2D().find(nodes[i])->asset==modernAssets[i],"unchanged mode reuses immutable pixels");
        require(sync(false).has_value() && playback.update(60).has_value() && playback.world2D().order()==nodes,"native fallback retains sequence identities");
        for(std::size_t i=0;i<nodes.size();++i)
        {
            const auto& restored=*playback.world2D().find(nodes[i]);
            require(restored.clock==runningClocks[i] && restored.worldTransform.values==native[i].worldTransform.values &&
                restored.asset->image.pixels==native[i].asset->image.pixels && !restored.asset->preferLinearFiltering && !restored.asset->presentationRect,"native fallback restores exact pixels geometry and flags");
        }
    }
    void testName()
    {
        Fixture f; engine::SequencePlayback playback(f.resources.service.snapshot()); tradeui::NamePlayback owner;
        const auto sync=[&](bool modern){return owner.sync(f.state,f.game,display::Screen2D::Trade,&f.font,playback,modern);};
        qualifyModes(f,playback,sync);
        require(sync(true).has_value() && playback.update(60).has_value(),"restore modern names");
        const auto node=playback.world2D().order().front();
        const auto id=playback.world2D().find(node)->contentsDataId;
        const auto before=playback.runtimeBitmaps().asset(id); f.font.setItalic(true);
        require(sync(true).has_value() && playback.update(60).has_value(),"name font characteristics refresh pixels");
        const auto changed=playback.runtimeBitmaps().asset(id);
        require(changed!=before && f.font.settings().italic && sync(true).has_value() && playback.commands().pendingCount()==0 && playback.runtimeBitmaps().asset(id)==changed,"name font cache refreshes once and restores caller");
    }
    void testPanel()
    {
        Fixture f; engine::SequencePlayback playback(f.resources.service.snapshot()); tradeui::PanelTextPlayback owner;
        const auto sync=[&](bool modern){return owner.sync(f.state,display::Screen2D::Trade,&f.font,playback,modern);};
        qualifyModes(f,playback,sync);
        require(sync(true).has_value() && playback.update(60).has_value(),"restore modern panel");
        const auto node=playback.world2D().order().front();
        const auto id=playback.world2D().find(node)->contentsDataId;
        const auto before=playback.runtimeBitmaps().asset(id); require(f.font.setSize(19).has_value(),"change external panel font setting");
        require(sync(true).has_value() && playback.update(60).has_value(),"panel font-setting cache refreshes pixels");
        const auto changed=playback.runtimeBitmaps().asset(id);
        require(changed!=before && f.font.settings().size==19 && sync(true).has_value() && playback.commands().pendingCount()==0 && playback.runtimeBitmaps().asset(id)==changed,"panel saves refreshed font cache without restarting");
    }
    void testCash()
    {
        Fixture f; engine::SequencePlayback playback(f.resources.service.snapshot()); tradeui::CashTextPlayback owner;
        const auto sync=[&](bool modern){return owner.sync(f.state,f.game,display::Screen2D::Trade,13,&f.font,playback,modern);};
        qualifyModes(f,playback,sync);
        require(sync(true).has_value() && playback.update(60).has_value(),"restore modern cash");
        data::DataId id{};
        for(const auto node:playback.world2D().order()) if(data::isRuntimeBitmapDataId(playback.world2D().find(node)->contentsDataId)){id=playback.world2D().find(node)->contentsDataId;break;}
        const auto before=playback.runtimeBitmaps().asset(id);
        require(f.font.setSize(9).has_value() && f.font.saveSettings(8).has_value(),"change authoritative cash font slot");
        require(sync(true).has_value() && playback.update(60).has_value(),"cash font slot refreshes pixels");
        const auto changed=playback.runtimeBitmaps().asset(id);
        require(changed!=before && f.font.settings().size==9 && sync(true).has_value() && playback.commands().pendingCount()==0 && playback.runtimeBitmaps().asset(id)==changed,"cash caches selected font slot and restores caller");
    }
}
int main()
{
    try { testName(); testPanel(); testCash(); std::cout<<"[PASS] Trade name/panel/cash modern typography and native fallback\n"; }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
