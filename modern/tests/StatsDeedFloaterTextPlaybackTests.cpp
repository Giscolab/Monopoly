#include "TextRefreshProof.hpp"
#include "StatsDeedFloaterTextPlayback.hpp"
#include "SyntheticTextResources.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace monopoly;

namespace
{
    void require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
        std::cout << "[PASS] " << message << '\n';
    }

    SyntheticTextResources::Texts texts()
    {
        return {
            {900, u"BANK"},
            {3205, u"OWNER"},
            {3206, u"CURRENT RENT"},
            {3207, u"GAME EARNINGS"},
            {3208, u"FUTURE VALUE TO YOU"}
        };
    }

    statsui::State deedState(int x, int y)
    {
        statsui::State state{};
        state.screen = statsui::Screen::Deed;
        state.activeSort = 0;
        state.mouseKnown = true;
        state.mouseX = x;
        state.mouseY = y;
        for (std::size_t i = 0; i < rules::SquareCount; ++i)
            state.deedOrder[i] = static_cast<std::uint8_t>(i);
        return state;
    }

    rules::GameState game()
    {
        rules::GameState state{};
        state.numberOfPlayers = 1;
        state.players[0].name = L"Alice";
        state.squares[1].owner = 0;
        state.squares[1].gameEarnings = 123;
        return state;
    }

    const engine::SequenceWorld2DObject* at(
        engine::SequencePlayback& playback, int x, int y)
    {
        for (const auto node : playback.world2D().order())
        {
            const auto* object = playback.world2D().find(node);
            if (object &&
                object->worldTransform.values[6] == static_cast<float>(x) &&
                object->worldTransform.values[7] == static_cast<float>(y))
                return object;
        }
        return nullptr;
    }

    void testTextLifecycle()
    {
        SyntheticTextResources resources(texts());
        fonts::Runtime font;
        loadRealTestArial(font);
        require(font.saveSettings(0).has_value(),
            "save default Arial for deed floater text");
        const auto original = font.settings();
        engine::SequencePlayback playback(resources.service.snapshot());
        statsui::DeedFloaterTextPlayback owner;
        auto state = deedState(24, 236);
        auto rules = game();

        require(owner.sync(state, rules, {}, 13,
                display::Screen2D::Portfolio, &font, playback).has_value(),
            "deed hover rasterizes owner/rent/earnings/future text");
        require(playback.commands().pendingCount() == 1 &&
                playback.runtimeBitmaps().size() == 1,
            "first deed text publication allocates one runtime surface");
        require(font.settings() == original,
            "deed floater text restores caller font settings");
        require(playback.update(0).has_value(),
            "deed floater text Start reaches SequencePlayback");

        const auto* right = at(playback, 410, 220);
        require(right && right->priority ==
                statsui::DeedFloaterTextPriority &&
                right->asset && right->asset->image.width == 400 &&
                right->asset->image.height == 235,
            "left-half hover places 400x235 text overlay on retail right side");
        require(std::any_of(right->asset->image.pixels.begin(),
                right->asset->image.pixels.end(),
                [](std::uint8_t value) { return value != 0; }),
            "deed text overlay contains real rendered pixels");
        const auto firstAsset = right->asset;

        const auto rootsBeforeRefresh = playback.runtime().roots();
        rules.squares[1].gameEarnings = 456;
        require(textRefreshRejectsFullQueue(playback, [&] { return owner.sync(state, rules, {}, 13, display::Screen2D::Portfolio, &font, playback); }),
            "saturated refresh preserves old pixels and roots for a later retry");
        require(owner.sync(state, rules, {}, 13,
                display::Screen2D::Portfolio, &font, playback).has_value() &&
                playback.commands().pendingCount() == 1,
            "changing deed earnings rerasterizes without restarting root");
        const auto refreshed =
            playback.runtimeBitmaps().asset(firstAsset->dataId);
        require(refreshed && refreshed != firstAsset,
            "deed earnings update publishes immutable bitmap revision");
        require(textRefreshPreservesRoots(playback, rootsBeforeRefresh, 1),
            "active deed text root observes refreshed bitmap revision");
        require(at(playback, 410, 220) && at(playback, 410, 220)->asset == refreshed,
            "original deed text node presents the new immutable bitmap");

        state.mouseX = 470;
        state.mouseY = 236;
        require(owner.sync(state, rules, {}, 13,
                display::Screen2D::Portfolio, &font, playback).has_value() &&
                playback.commands().pendingCount() == 2,
            "crossing screen half queues Stop/Start for text-side move");
        require(playback.update(2).has_value() &&
                at(playback, 10, 220) != nullptr,
            "right-half hover moves text overlay to retail left side");

        state.mouseX = 790;
        state.mouseY = 440;
        require(owner.sync(state, rules, {}, 13,
                display::Screen2D::Portfolio, nullptr, playback).has_value() &&
                playback.commands().pendingCount() == 1,
            "leaving deeds hides text without needing font runtime");
        require(playback.update(3).has_value() &&
                playback.world2D().size() == 0,
            "deed floater text Stop removes runtime overlay");
    }

    void testModernPresentation()
    {
        SyntheticTextResources resources(texts());fonts::Runtime font;loadRealTestArial(font);
        require(font.saveSettings(0).has_value(),"save authoritative floater font");
        const auto originalFont=font.settings();
        engine::SequencePlayback playback(resources.service.snapshot());statsui::DeedFloaterTextPlayback owner;
        auto state=deedState(24,236);const auto rules=game();
        auto sync=[&](bool modern){return owner.sync(state,rules,{},13,display::Screen2D::Portfolio,&font,playback,false,modern);};
        require(sync(false).has_value() && playback.update(0).has_value() && playback.update(60).has_value(),"native floater has actual glyphs at clock60");
        const auto native=*at(playback,410,220);const auto roots=playback.runtime().roots();
        const auto nodes=playback.world2D().order();
        std::uint64_t nativeHash=14695981039346656037ULL;
        for(const auto byte:native.asset->image.pixels){nativeHash^=byte;nativeHash*=1099511628211ULL;}
        std::cout<<"native deed floater RGBA FNV1a64="<<std::hex<<nativeHash<<std::dec<<'\n';
        require(textRefreshRejectsFullQueue(playback,[&]{return sync(true);},60),"saturated modern refresh preserves native raster and queue clocks");
        require(font.setSize(19).has_value(),"set unrelated caller font size");font.setItalic(true);font.setUnderline(true);font.setStrikeOut(true);font.setWeight(900);
        const auto caller=font.settings();
        require(sync(true).has_value() && font.settings()==caller && playback.commands().pendingCount()==1 && playback.update(60).has_value(),
            "modern floater restores full caller size/style and refreshes once");
        const auto modern=*at(playback,410,220);
        require(modern.clock==native.clock && modern.priority==native.priority && modern.contentsDataId==native.contentsDataId &&
            playback.runtime().roots()==roots && playback.world2D().order()==nodes &&
            engine::SequenceWorld2DSlot::transformPoint(modern.worldTransform,0,0)==engine::SequenceWorld2DSlot::transformPoint(native.worldTransform,0,0) &&
            engine::SequenceWorld2DSlot::transformPoint(modern.worldTransform,1200,705)==engine::SequenceWorld2DSlot::transformPoint(native.worldTransform,400,235),
            "modern floater keeps original nodes roots clock60 priority and placement");
        require(modern.asset->image.width==1200 && modern.asset->image.height==705 && modern.asset->preferLinearFiltering &&
            modern.asset->presentationRect==std::optional<std::array<float,4>>{{0,0,400,235}},
            "modern floater rasterizes3x with exact logical400x235 footprint");
        bool blended=false,transparent=false;
        for(std::size_t i=3;i<modern.asset->image.pixels.size();i+=4){blended|=modern.asset->image.pixels[i]>0&&modern.asset->image.pixels[i]<255;transparent|=modern.asset->image.pixels[i]==0;}
        require(blended && transparent,"actual glyph raster includes blended coverage and transparent layout");
        const auto separator=(std::size_t(15)*1200+193*3)*4;
        require(modern.asset->image.pixels[separator]==13 && modern.asset->image.pixels[separator+1]==35 && modern.asset->image.pixels[separator+2]==38,
            "modern separator uses deepteal in unchanged authored rectangle");
        require(sync(true).has_value() && playback.commands().pendingCount()==0 && playback.runtimeBitmaps().asset(modern.contentsDataId)==modern.asset,
            "unchanged effective font and content reuse modern immutable cache");
        require(font.setSize(23).has_value() && sync(true).has_value() && font.settings().size==23 && playback.commands().pendingCount()==0,
            "unrelated caller size leaves logical metrics and cache unchanged");
        const auto warmCaller=font.settings();
        const auto warmSaved=*font.savedSettings(0);
        for(unsigned frame=0;frame<100;++frame)
            require(sync(true).has_value() && playback.commands().pendingCount()==0 && font.settings()==warmCaller &&
                *font.savedSettings(0)==warmSaved && playback.runtimeBitmaps().asset(modern.contentsDataId)==modern.asset,
                "100 warm floater cache probes preserve caller/saved font and immutable raster without redraw");
        font.setItalic(true);require(font.saveSettings(0).has_value() && sync(true).has_value() && playback.update(60).has_value(),
            "effective saved face style refreshes modern glyphs at same root clock");
        require(at(playback,410,220)->asset!=modern.asset && at(playback,410,220)->clock==native.clock,
            "effective font cache invalidates without restarting floater");
        require(font.setSize(originalFont.size).has_value(),"restore original saved font size");font.setWeight(originalFont.weight);
        font.setItalic(originalFont.italic);font.setUnderline(originalFont.underline);font.setStrikeOut(originalFont.strikeOut);
        require(font.saveSettings(0).has_value() && sync(false).has_value() && playback.update(60).has_value(),"return to native floater presentation");
        const auto restored=*at(playback,410,220);
        require(restored.asset->image.pixels==native.asset->image.pixels && !restored.asset->presentationRect && !restored.asset->preferLinearFiltering &&
            restored.clock==native.clock && restored.worldTransform.values==native.worldTransform.values,
            "native branch restores exact pre-modern RGBA hash/pixels and geometry");
        require(!owner.sync(state,rules,{},13,display::Screen2D::Portfolio,nullptr,playback,false,true) && playback.commands().pendingCount()==0 &&
            playback.runtimeBitmaps().asset(restored.contentsDataId)==restored.asset,"modern missing-font failure leaves native cache and queue intact");
    }
    void testPopupHideAndRestore()
    {
        SyntheticTextResources resources(texts());
        fonts::Runtime font;
        loadRealTestArial(font);
        require(font.saveSettings(0).has_value(), "save popup-test font defaults");
        engine::SequencePlayback playback(resources.service.snapshot());
        statsui::DeedFloaterTextPlayback owner;
        auto state = deedState(245, 250);
        const auto rules = game();
        require(owner.sync(state, rules, {}, 13, display::Screen2D::Portfolio,
                &font, playback).has_value() && playback.update(0).has_value(),
            "normal deed text exists before overlapping calculator popup opens");
        const auto* before = at(playback, 410, 220);
        require(before && before->asset, "normal floater has a real rasterized surface");
        const auto surfaceId = before->asset->dataId;
        const auto pixels = before->asset->image.pixels;
        require(owner.sync(state, rules, {}, 13, display::Screen2D::Portfolio,
                nullptr, playback, true).has_value() && playback.update(1).has_value() &&
                playback.world2D().size() == 0,
            "opening popup stops normal text even without a ready font");
        require(owner.sync(state, rules, {}, 13, display::Screen2D::Portfolio,
                nullptr, playback, true).has_value() && playback.commands().pendingCount() == 0,
            "stationary popup hover keeps normal text hidden");
        require(owner.sync(state, rules, {}, 13, display::Screen2D::Portfolio,
                &font, playback, false).has_value() && playback.update(2).has_value(),
            "closing popup restores normal text without another mouse move");
        const auto* after = at(playback, 410, 220);
        require(after && after->asset && after->asset->dataId == surfaceId &&
                after->asset->image.pixels == pixels && playback.runtimeBitmaps().size() == 1,
            "restored normal text reuses its surface and preserves rasterized content");
    }

    void testFailures()
    {
        SyntheticTextResources resources(texts());
        auto rules = game();
        auto state = deedState(24, 236);

        engine::SequencePlayback noFont(resources.service.snapshot());
        statsui::DeedFloaterTextPlayback owner;
        require(!owner.sync(state, rules, {}, 13,
                display::Screen2D::Portfolio, nullptr, noFont),
            "visible deed text rejects missing font runtime");
        require(noFont.commands().pendingCount() == 0 &&
                noFont.runtimeBitmaps().size() == 0,
            "missing font fails before queue or surface mutation");

        fonts::Runtime font;
        loadRealTestArial(font);
        require(font.saveSettings(0).has_value(),
            "save failure-test Arial defaults");
        engine::SequencePlayback saturated(resources.service.snapshot());
        for (std::size_t index = 0;
             index < sequence::SequenceCommandQueue::Capacity; ++index)
        {
            if (!saturated.commands().enqueue(
                    sequence::StopSequenceCommand{
                        data::packDataId(data::LegacyGroupId::Main, 1), 999}))
                throw std::runtime_error(
                    "failed to fill deed-floater text command queue");
        }
        const auto before = saturated.commands().pendingCount();
        statsui::DeedFloaterTextPlayback saturatedOwner;
        require(!saturatedOwner.sync(state, rules, {}, 13,
                display::Screen2D::Portfolio, &font, saturated),
            "deed text preflights a saturated sequence queue");
        require(saturated.commands().pendingCount() == before &&
                saturated.runtimeBitmaps().size() == 0,
            "queue saturation fails before surface mutation");
    }
}

int main()
{
    try
    {
        testTextLifecycle();
        testModernPresentation();
        testFailures();
        testPopupHideAndRestore();
        std::cout << "Stats Deed floater text playback tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
