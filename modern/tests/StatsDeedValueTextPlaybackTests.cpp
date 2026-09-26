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
