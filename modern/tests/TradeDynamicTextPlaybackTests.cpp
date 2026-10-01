#include "TradeNamePlayback.hpp"
#include "TradePanelTextPlayback.hpp"
#include "TradeCashTextPlayback.hpp"
#include "SyntheticTextResources.hpp"
#include "LanguageResources.hpp"

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
    struct PanelProof
    {
        data::LegacyBitmapRGBA8 image{600,675,std::vector<std::uint8_t>(600*675*4,0)};
        unsigned lines{};
        int bottom{};
    };
    PanelProof fullPanelProof(std::string_view title, std::string_view body)
    {
        // Independently assemble every real LANG line with the declared style;
        // unlike the publishing blitter, this oracle rejects any clipped raster.
        fonts::Runtime font; loadRealTestArial(font); PanelProof proof; int y=14;
        const auto paragraph=[&](std::string_view text,int size,int weight)
        {
            require(font.setSize(size).has_value(),"Panel proof selects native font size");
            font.setWeight(weight);font.setUnderline(false);font.setItalic(false);font.setStrikeOut(false);
            const auto lines=font.wrap(text,176);require(lines.has_value(),"Complete authentic paragraph wraps at176");
            int advance=0;
            if(!lines->empty()){const auto metrics=font.measure(lines->front());require(metrics.has_value(),"Panel line advance measures");advance=metrics->height;}
            for(const auto& line:*lines)
            {
                if(!line.empty())
                {
                    const auto glyph=font.renderPresentation(line,0x00D3EBF5U,size*3,weight,false,false,false,true);
                    require(glyph && glyph->width<=600 && y*3+int(glyph->height)<=675,
                        "Every title/body glyph fits entirely inside the authored200x225 panel");
                    require(data::blitStraightRGBA8(proof.image,*glyph,(600-int(glyph->width))/2,y*3,
                        data::BitmapBlitMode::SourceOver).has_value(),"Complete line contributes its actual private-font pixels");
                    proof.bottom=y*3+int(glyph->height);++proof.lines;
                }
                y+=advance;
            }
        };
        paragraph(title,12,600);y+=3;paragraph(body,10,400);return proof;
    }
    void testPanelTypography()
    {
        Fixture f;engine::SequencePlayback playback(f.resources.service.snapshot());tradeui::PanelTextPlayback owner;
        require(f.font.setSize(17).has_value(),"Distinct caller font");f.font.setItalic(true);f.font.setUnderline(true);f.font.setStrikeOut(true);
        const auto caller=f.font.settings();const auto slot0=*f.font.savedSettings(0);const auto slot8=*f.font.savedSettings(8);
        const auto synced=owner.sync(f.state,display::Screen2D::Trade,&f.font,playback,true);
        if(!synced)throw std::runtime_error(synced.error());
        require(playback.update(0).has_value(),"Modern panel runs original root");
        const auto* node=playback.world2D().find(playback.world2D().order().front());
        const auto expected=fullPanelProof("Trading description heading",
            "Several trading instructions wrap across native measured lines without changing the authoritative panel layout.");
        require(expected.lines>2 && node->asset->image.pixels==expected.image.pixels,
            "Modern panel contains ALL source lines in12semibold unadorned title and10regular cream body");
        require(f.font.settings()==caller && *f.font.savedSettings(0)==slot0 && *f.font.savedSettings(8)==slot8,
            "Private AA rendering restores caller style and leaves original saved font slots untouched");
        require(node->priority==148 && node->worldTransform.values[6]==0 && node->worldTransform.values[7]==0,
            "Typography retains original panel priority and origin");
    }
    void testPanelOverflowFallback()
    {
        const std::u16string longText(3000,u'W');
        SyntheticTextResources resources{{{2013,u"Trading"},{2014,longText}}};fonts::Runtime font;loadRealTestArial(font);
        require(font.saveSettings(0).has_value(),"Overflow default font slot");
        tradeui::State state;state.playerA=0;engine::SequencePlayback playback(resources.service.snapshot());tradeui::PanelTextPlayback owner;
        require(owner.sync(state,display::Screen2D::Trade,&font,playback,false).has_value() && playback.update(0).has_value(),"Native oversized-content baseline");
        const auto id=playback.world2D().find(playback.world2D().order().front())->contentsDataId;
        const auto native=playback.runtimeBitmaps().asset(id);const auto caller=font.settings();
        require(owner.sync(state,display::Screen2D::Trade,&font,playback,true).has_value() && playback.update(0).has_value(),"Unfitting modern paragraph falls back without fatal error");
        const auto fallback=playback.runtimeBitmaps().asset(id);
        require(fallback->image.pixels==native->image.pixels && fallback->image.width==200 && !fallback->preferLinearFiltering &&
            !fallback->presentationRect && font.settings()==caller,"Unfitting paragraph retains exact native bytes/footprint/caller settings instead of publishing clipped AA");
        require(owner.sync(state,display::Screen2D::Trade,&font,playback,true).has_value() && playback.commands().pendingCount()==0 &&
            playback.runtimeBitmaps().asset(id)==fallback,"Fit failure is cached without reraster or command churn");
    }
    void qualifyRetailPanel(const std::filesystem::path& root)
    {
        const auto paths=data::ResourcePaths::create(std::array{root});data::ResourceRuntime resources;
        require(paths && resources.initialize(*paths).has_value(),"Actual panel DAT resources load");
        const auto catalog=resources.snapshot()->language();require(catalog && catalog->catalog,"Actual panel LANG catalog");
        const auto text=[&](unsigned id)
        {
            const auto message=catalog->catalog->message(id);require(message.has_value(),"Actual LANG2013/2014 message");
            const auto utf8=fonts::transcodeUtf8(std::u16string_view(**message));require(utf8.has_value(),"Authentic LANG transcodes without rewriting");return *utf8;
        };
        const auto title=text(2013),body=text(2014);const auto proof=fullPanelProof(title,body);
        fonts::Runtime font;loadRealTestArial(font);require(font.saveSettings(0).has_value(),"Actual default font slot");
        const auto settings=font.settings();engine::SequencePlayback playback(resources.snapshot());tradeui::PanelTextPlayback owner;tradeui::State state;state.playerA=0;
        require(owner.sync(state,display::Screen2D::Trade,&font,playback,true).has_value() && playback.update(0).has_value(),"Actual modern panel publication");
        const auto* node=playback.world2D().find(playback.world2D().order().front());
        require(node->asset->image.pixels==proof.image.pixels && node->asset->preferLinearFiltering && font.settings()==settings,
            "Actual complete LANG/Arial panel fits with every expected glyph and restored caller font");
        std::cout<<"[panel-retail] title="<<title<<" body="<<body<<" lines="<<proof.lines<<" rasterBottom="<<proof.bottom<<"/675\n";
        std::cout<<"[PASS] authentic Trade instructions complete3x typography/200x225 footprint (CPU only)\n";
    }
    void testCashReadout()
    {
        Fixture f; engine::SequencePlayback playback(f.resources.service.snapshot()); tradeui::CashTextPlayback owner;
        const auto sync=[&](bool enabled=true, bool modern=true, display::Screen2D view=display::Screen2D::Trade)
        { return owner.sync(f.state,f.game,view,13,&f.font,playback,modern,enabled); };
        f.state.cashDialogVisible=true; f.state.cashTradeAmount=0;
        require(sync(false).has_value() && playback.update(60).has_value() && !owner.readoutSurface(),
            "unqualified native popup creates no fifth amount surface");
        const auto nodes=playback.world2D().order();
        std::vector<engine::SequenceWorld2DObject> original;
        for(const auto node:nodes)original.push_back(*playback.world2D().find(node));
        const auto caller=f.font.settings();
        const auto opened=sync();
        if(!opened)throw std::runtime_error("Cash readout sync: "+opened.error());
        require(f.font.settings()==caller && playback.update(60).has_value(),
            "qualified popup publishes readout without mutating the caller font");
        require(owner.readoutSurface().has_value() && playback.runtimeBitmaps().size()==5,
            "exactly one additional retained amount surface is owned");
        const auto id=*owner.readoutSurface();
        const auto findReadout=[&]() -> const engine::SequenceWorld2DObject* {
            for(const auto node:playback.world2D().order())
                if(playback.world2D().find(node)->contentsDataId==id)return playback.world2D().find(node);
            return nullptr;
        };
        const auto* object=findReadout();
        require(object && object->priority==1978 && object->worldTransform.values[6]==12 && object->worldTransform.values[7]==325,
            "amount is above popup controls and inside the empty left header");
        const auto initial=playback.runtimeBitmaps().asset(id);
        require(initial && initial->image.width==516 && initial->image.height==42 && initial->preferLinearFiltering &&
            initial->presentationRect==std::optional<std::array<float,4>>{{0,0,172,14}},
            "AA amount occupies exactly172x14 logical pixels above native notes");
        for(std::size_t i=0;i<nodes.size();++i)
        {
            const auto* retained=playback.world2D().find(nodes[i]);
            require(retained && retained->clock==original[i].clock && retained->asset==original[i].asset &&
                retained->worldTransform.values==original[i].worldTransform.values,
                "opening popup retains every existing cash field/icon clock pixels and placement");
        }
        const auto glyph=f.font.renderPresentation("$ 0",0x00D3EBF5U,27,600,false,false,false,true);
        require(glyph.has_value(),"actual currency glyphs render using the retained font face");
        data::LegacyBitmapRGBA8 expected{516,42,{}};expected.pixels.assign(516*42*4,0);
        require(data::blitStraightRGBA8(expected,*glyph,(516-int(glyph->width))/2,(42-int(glyph->height))/2,
            data::BitmapBlitMode::SourceOver).has_value() && expected.pixels==initial->image.pixels,
            "popup shows exact authentic USD zero with centered private-font AA pixels");
        require(sync().has_value() && playback.commands().pendingCount()==0 && playback.runtimeBitmaps().asset(id)==initial,
            "unchanged popup reuses owned pixels and emits no commands");
        require(playback.update(120).has_value(),"advance amount clock");
        const auto clock=findReadout()->clock;
        f.state.cashTradeAmount=50;
        require(sync().has_value() && playback.update(120).has_value() && findReadout()->clock==clock &&
            playback.runtimeBitmaps().asset(id)!=initial && f.font.settings()==caller,
            "cash edit refreshes amount pixels without restarting sequence clock or changing font");
        f.state.cashDialogSide=1;
        require(sync().has_value() && playback.update(120).has_value() && findReadout()->worldTransform.values[6]==612,
            "same owned readout follows the actual right popup");
        std::vector<std::int32_t> beforeHide;
        for(const auto node:nodes)beforeHide.push_back(playback.world2D().find(node)->clock);
        f.state.cashDialogVisible=false;
        require(sync().has_value() && playback.update(120).has_value() && !findReadout() && playback.runtimeBitmaps().size()==5,
            "closed popup retires only its fifth root while keeping bounded reusable pixels");
        for(std::size_t i=0;i<nodes.size();++i)
            require(playback.world2D().find(nodes[i])->clock==beforeHide[i],
                "readout removal preserves all existing cash field and icon clocks");
        f.state.cashDialogVisible=true;
        require(sync(false).has_value() && playback.update(120).has_value() && !findReadout(),
            "context qualification is checked before cached readout reuse");
        require(sync(true,false).has_value() && playback.update(120).has_value() && !findReadout(),
            "native typography never publishes modern amount readout");
        f.state.cashDialogSide=2;
        require(sync().has_value() && playback.update(120).has_value() && !findReadout(),
            "unknown popup side fails closed without invalid placement");
        f.state.cashDialogSide=0;
        require(sync().has_value() && playback.update(120).has_value() && findReadout(),"reopen reuses one amount asset");
        f.state.playerSelectVisible=true;
        require(sync().has_value() && playback.update(120).has_value() && !findReadout(),
            "partner chooser suppresses amount readout");
        owner.reset();playback.runtimeBitmaps().clear();
        require(!owner.readoutSurface() && playback.runtimeBitmaps().size()==0,"session reset clears readout ownership");
    }

    void testNativeCashVisibilityClocks()
    {
        Fixture f; engine::SequencePlayback playback(f.resources.service.snapshot()); tradeui::CashTextPlayback owner;
        const auto sync=[&](bool modern=false, bool readout=false)
        { return owner.sync(f.state,f.game,display::Screen2D::Trade,13,&f.font,playback,modern,readout); };
        require(sync().has_value() && playback.update(0).has_value() && playback.update(60).has_value(),
            "native cash fields advance before visibility transition");
        bool running=false;
        for(const auto node:playback.world2D().order())
            if(data::isRuntimeBitmapDataId(playback.world2D().find(node)->contentsDataId))
                running|=playback.world2D().find(node)->clock==60;
        require(running,"native raw cash clocks are running");
        f.state.cashDesired[2]=0;
        require(sync().has_value() && playback.update(60).has_value(),"native offered field disappears");
        for(const auto node:playback.world2D().order())
            if(data::isRuntimeBitmapDataId(playback.world2D().find(node)->contentsDataId))
                require(playback.world2D().find(node)->clock==0,
                    "native visibility transition retains historical stop/start clock contract");
        f.state.cashDialogVisible=true;
        require(sync(true,true).has_value() && playback.update(60).has_value() && owner.readoutSurface(),
            "allocate and publish modern readout");
        f.state.cashDialogVisible=false;
        require(sync(true,true).has_value() && playback.update(60).has_value() && playback.update(120).has_value(),
            "hide modern readout while retaining its cached asset");
        f.state.cashDesired[3]=0;
        require(sync().has_value() && playback.update(120).has_value(),"native transition after readout was retired");
        for(const auto node:playback.world2D().order())
            if(data::isRuntimeBitmapDataId(playback.world2D().find(node)->contentsDataId))
                require(playback.world2D().find(node)->clock==0,
                    "allocated but unpublished readout does not change subsequent native clock behavior");
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
int main(int argc,char** argv)
{
    try { if(argc==3 && std::string_view(argv[1])=="--panel-retail-qualify"){qualifyRetailPanel(argv[2]);return 0;} testName(); testPanel(); testPanelTypography(); testPanelOverflowFallback(); testCash(); testCashReadout(); testNativeCashVisibilityClocks(); std::cout<<"[PASS] Trade name/panel/cash modern typography and native fallback\n"; }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
