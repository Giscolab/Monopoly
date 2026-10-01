#include "TextRefreshProof.hpp"
#include "StatsDeedValueTextPlayback.hpp"
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

    statsui::State deedState()
    {
        statsui::State state{};
        state.screen = statsui::Screen::Deed;
        state.activeSort = 0;
        for (std::size_t rank = 0; rank < rules::SquareCount; ++rank)
        {
            state.deedOrder[rank] = static_cast<std::uint8_t>(rank);
            state.deedMetric[rank] =
                static_cast<std::int64_t>(100 + rank);
        }
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

    void testValuesAndRefresh()
    {
        SyntheticTextResources resources;
        fonts::Runtime font;
        loadRealTestArial(font);
        require(font.saveSettings(0).has_value(),
            "save default Arial settings for deed value bars");
        const auto original = font.settings();

        engine::SequencePlayback playback(resources.service.snapshot());
        statsui::DeedValueTextPlayback owner;
        auto state = deedState();
        rules::GameState game{};

        require(owner.sync(state, game, {}, 13,
                display::Screen2D::Portfolio, &font, playback).has_value(),
            "purchase-value sort rasterizes all visible deed values");
        require(playback.commands().pendingCount() == 28 &&
                playback.runtimeBitmaps().size() == 28,
            "purchase-value sort creates 28 runtime value surfaces");
        require(font.settings() == original,
            "deed value renderer restores caller font settings");
        require(playback.update(0).has_value() &&
                playback.world2D().size() == 28,
            "28 deed values publish to Overlay2D");

        const auto* first = at(playback, 64, 247);
        require(first && first->priority ==
                statsui::DeedValueTextPriority &&
                first->asset && first->asset->image.width == 52 &&
                first->asset->image.height == 13,
            "first deed value uses retail 52x13 bar origin above TAB_dvaldisp");
        require(std::any_of(first->asset->image.pixels.begin(),
                first->asset->image.pixels.end(),
                [](std::uint8_t value) { return value != 0; }),
            "deed value contains real gray font pixels");
        const auto firstAsset = first->asset;

        const auto rootsBeforeRefresh = playback.runtime().roots();
        state.deedMetric[1] = 98765;
        require(textRefreshRejectsFullQueue(playback, [&] { return owner.sync(state, game, {}, 13, display::Screen2D::Portfolio, &font, playback); }),
            "saturated refresh preserves old pixels and roots for a later retry");
        require(owner.sync(state, game, {}, 13,
                display::Screen2D::Portfolio, &font, playback).has_value() &&
                playback.commands().pendingCount() == 1,
            "value change rerasterizes without restarting stable bar roots");
        const auto refreshed =
            playback.runtimeBitmaps().asset(firstAsset->dataId);
        require(refreshed && refreshed != firstAsset,
            "updated deed value publishes an immutable bitmap revision");
        require(textRefreshPreservesRoots(playback, rootsBeforeRefresh, 1),
            "active value root observes refreshed runtime bitmap");
        require(at(playback, 64, 247) && at(playback, 64, 247)->asset == refreshed,
            "original value node presents the new immutable bitmap");

        state.activeSort = 1;
        require(owner.sync(state, game, {}, 13,
                display::Screen2D::Portfolio, nullptr, playback).has_value() &&
                playback.commands().pendingCount() == 28,
            "Owner sort removes numeric text without requiring font runtime");
        require(playback.update(2).has_value() &&
                playback.world2D().size() == 0,
            "Owner sort leaves color bars to the separate DeedBar owner");

        state.activeSort = 3;
        state.deedMetric.fill(0);
        state.deedMetric[1] = 500;
        require(owner.sync(state, game, {}, 13,
                display::Screen2D::Portfolio, &font, playback).has_value() &&
                playback.commands().pendingCount() == 1,
            "Most Valuable compacts zero earnings and publishes only nonzero deed");
        require(playback.update(3).has_value() &&
                playback.world2D().size() == 1,
            "Most Valuable value text follows compacted deed grid");
    }

    void testModernPresentation()
    {
        SyntheticTextResources resources; fonts::Runtime font; loadRealTestArial(font);
        require(font.saveSettings(0).has_value(),"save modern deed-value native font");
        engine::SequencePlayback playback(resources.service.snapshot());
        statsui::DeedValueTextPlayback owner; auto state=deedState(); rules::GameState game{};
        const auto sync=[&](bool modern){return owner.sync(state,game,{},13,display::Screen2D::Portfolio,&font,playback,modern);};
        require(sync(false).has_value() && playback.update(0).has_value() && playback.update(60).has_value(),"native deed values reach original sixty-tick clock");
        const auto nodes=playback.world2D().order(); const auto roots=playback.runtime().roots();
        const auto native=*playback.world2D().find(nodes.front());
        require(native.clock==60 && !native.asset->presentationRect && !native.asset->preferLinearFiltering,"native deed-value presentation and clock are authoritative");
        require(textRefreshRejectsFullQueue(playback,[&]{return sync(true);},60),"saturated modern mode refresh preserves native pixels and roots");
        require(playback.world2D().find(nodes.front())->clock==native.clock && native.clock==60,"rejected refresh and same-tick queue drain preserve native clock60");
        const auto caller=font.settings();
        require(sync(true).has_value() && playback.commands().pendingCount()==28 && font.settings()==caller,"modern values refresh existing surfaces and restore caller font");
        require(playback.update(60).has_value() && playback.world2D().order()==nodes && playback.runtime().roots()==roots,"modern glyph mode retains every deed sequence node and root");
        const auto modern=*playback.world2D().find(nodes.front());
        require(modern.clock==native.clock && modern.priority==native.priority && modern.contentsDataId==native.contentsDataId &&
            modern.worldTransform.values[6]==native.worldTransform.values[6] && modern.worldTransform.values[7]==native.worldTransform.values[7],"modern values preserve clock priority identity and native right-aligned placement");
        require(modern.asset->image.width==156 && modern.asset->image.height==39 && modern.asset->preferLinearFiltering &&
            modern.asset->presentationRect==std::optional<std::array<float,4>>{{0,0,52,13}},"modern values rasterize at three times native logical52x13 strip");
        bool coverage=false,transparent=false;
        for(std::size_t i=3;i<modern.asset->image.pixels.size();i+=4)
        {coverage|=modern.asset->image.pixels[i]>0 && modern.asset->image.pixels[i]<255;transparent|=modern.asset->image.pixels[i]==0;}
        require(coverage && transparent,"actual deed glyphs keep blended coverage on transparent strip");
        require(sync(true).has_value() && playback.commands().pendingCount()==0 && playback.runtimeBitmaps().asset(modern.contentsDataId)==modern.asset,"unchanged modern deed values reuse cached immutable pixels");
        require(font.setSize(19).has_value(),"change caller font setting for deed-value cache");
        require(sync(true).has_value() && playback.commands().pendingCount()==0 && font.settings().size==19 && playback.runtimeBitmaps().asset(modern.contentsDataId)==modern.asset,"unrelated caller size leaves authoritative slot0 glyph cache unchanged and restores caller");
        font.setItalic(true);
        require(font.saveSettings(0).has_value() && sync(true).has_value() && playback.update(60).has_value() && font.settings().size==19 && font.settings().italic,"effective saved font style refreshes modern values without resetting clocks");
        const auto changed=playback.runtimeBitmaps().asset(modern.contentsDataId);
        require(changed!=modern.asset && sync(true).has_value() && playback.commands().pendingCount()==0 && playback.runtimeBitmaps().asset(modern.contentsDataId)==changed,"modern deed font cache remembers refreshed caller settings");
        require(font.setSize(caller.size).has_value(),"restore original native caller size");
        font.setItalic(caller.italic);
        require(font.saveSettings(0).has_value() && sync(false).has_value() && playback.update(60).has_value(),"native mode restores after modern font-cache refresh");
        const auto restored=*playback.world2D().find(nodes.front());
        require(restored.clock==native.clock && restored.worldTransform.values==native.worldTransform.values && restored.asset->image.pixels==native.asset->image.pixels &&
            !restored.asset->preferLinearFiltering && !restored.asset->presentationRect,"native fallback restores exact deed-value pixels geometry and flags");
    }

    void testFailurePreflight()
    {
        SyntheticTextResources resources;
        fonts::Runtime font;
        loadRealTestArial(font);
        require(font.saveSettings(0).has_value(),
            "save preflight-test Arial defaults");

        auto state = deedState();
        rules::GameState game{};
        engine::SequencePlayback saturated(resources.service.snapshot());
        for (std::size_t index = 0;
             index < sequence::SequenceCommandQueue::Capacity; ++index)
        {
            if (!saturated.commands().enqueue(
                    sequence::StopSequenceCommand{
                        data::packDataId(data::LegacyGroupId::Main, 1), 999}))
                throw std::runtime_error(
                    "failed to fill deed-value command queue");
        }
        const auto before = saturated.commands().pendingCount();
        statsui::DeedValueTextPlayback owner;
        require(!owner.sync(state, game, {}, 13,
                display::Screen2D::Portfolio, &font, saturated),
            "deed value text preflights saturated sequence queue");
        require(saturated.commands().pendingCount() == before &&
                saturated.runtimeBitmaps().size() == 0,
            "queue failure occurs before runtime value-surface allocation");
    }
}

int main()
{
    try
    {
        testValuesAndRefresh();
        testModernPresentation();
        testFailurePreflight();
        std::cout << "Stats Deed value text playback tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
