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
