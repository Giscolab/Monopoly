#include "TextRefreshProof.hpp"
#include "StatsFutureImmunityTextPlayback.hpp"
#include "SyntheticTextResources.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace monopoly;

namespace
{
    void require(bool value, std::string_view message)
    {
        if (!value) throw std::runtime_error(std::string(message));
        std::cout << "[PASS] " << message << '\n';
    }

    SyntheticTextResources::Texts texts()
    {
        return {
            {3180, u"Futures for ^P"},
            {3181, u"Immunities for ^P"},
            {3182, u"Property"},
            {3183, u"Hits Remaining"},
            {1002, u"Mediterranean Avenue"},
            {1004, u"Baltic Avenue"}
        };
    }

    rules::GameState game()
    {
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.players[0].name = L"Alice";
        state.players[1].name = L"Bob";
        return state;
    }

    statsui::FutureImmunityState popup()
    {
        statsui::FutureImmunityState state{};
        state.open = true;
        state.kind = statsui::FutureImmunityKind::Future;
        state.player = 0;
        state.rows.push_back({1, 7, 1});
        state.rows.push_back({3, 2, 1});
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
    void configureFont(fonts::Runtime& font)
    {
        loadRealTestArial(font);
        require(font.saveSettings(0).has_value(),
            "save default Arial settings for Future/Immunity text");
    }

    void testVisibleAndRefresh()
    {
        SyntheticTextResources resources(texts());
        fonts::Runtime font;
        configureFont(font);
        const auto original = font.settings();

        engine::SequencePlayback playback(resources.service.snapshot());
        statsui::FutureImmunityTextPlayback owner;
        auto state = popup();
        const auto rules = game();

        require(owner.sync(state, rules, display::Screen2D::Portfolio,
                0, &font, playback).has_value(),
            "Future popup text rasterizes from LANG and rule rows");
        require(playback.commands().pendingCount() == 1 &&
                playback.runtimeBitmaps().size() == 1,
            "first popup text publication allocates one surface and one Start");
        require(font.settings() == original,
            "Future/Immunity renderer restores caller font settings");

        require(playback.update(0).has_value(),
            "Future/Immunity text Start reaches SequencePlayback");
        const auto* object = at(playback,
            statsui::FutureImmunityPopupRect.left,
            statsui::FutureImmunityPopupRect.top);
        require(object && object->priority ==
                statsui::FutureImmunityTextPriority &&
                object->asset && object->asset->image.width == 197 &&
                object->asset->image.height == 223,
            "text overlay is 197x223 at retail popup origin above board art");
        const auto firstAsset = object->asset;
        require(std::any_of(firstAsset->image.pixels.begin(),
                firstAsset->image.pixels.end(),
                [](std::uint8_t value) { return value != 0; }),
            "title, column labels and rows produce real font pixels");

        const auto rootsBeforeRefresh = playback.runtime().roots();
        state.scrollIndex = 1;
        require(textRefreshRejectsFullQueue(playback, [&] { return owner.sync(state, rules, display::Screen2D::Portfolio, 0, &font, playback); }),
            "saturated refresh preserves old pixels and roots for a later retry");
        require(owner.sync(state, rules, display::Screen2D::Portfolio,
                0, &font, playback).has_value(),
            "scrolling rerasterizes the same runtime surface");
        require(playback.commands().pendingCount() == 1,
            "scroll refresh does not restart the sequence root");
        const auto refreshedAsset =
            playback.runtimeBitmaps().asset(firstAsset->dataId);
        require(refreshedAsset && refreshedAsset != firstAsset,
            "scroll refresh publishes an immutable bitmap revision");

        require(textRefreshPreservesRoots(playback, rootsBeforeRefresh, 1),
            "next tick refreshes active Overlay2D bitmap revision");
        const auto* refreshed = at(playback,
            statsui::FutureImmunityPopupRect.left,
            statsui::FutureImmunityPopupRect.top);
        require(refreshed && refreshed->asset == refreshedAsset,
            "active Overlay2D root sees refreshed scroll contents");

        state.kind = statsui::FutureImmunityKind::Immunity;
        require(owner.sync(state, rules, display::Screen2D::Portfolio,
                0, &font, playback).has_value() &&
                playback.commands().pendingCount() == 1 &&
                textRefreshPreservesRoots(playback, rootsBeforeRefresh, 1),
            "Future-to-Immunity title change stays on the live surface");

        state.open = false;
        require(owner.sync(state, rules, display::Screen2D::Portfolio,
                0, nullptr, playback).has_value(),
            "hiding popup text needs no font runtime");
        require(playback.commands().pendingCount() == 1,
            "hiding queues exactly one text Stop");
        require(playback.update(2).has_value() &&
                playback.world2D().size() == 0,
            "popup text Stop removes the overlay root");
        require(playback.runtimeBitmaps().size() == 1,
            "hidden popup retains reusable text surface");
    }

    void testFailures()
    {
        SyntheticTextResources resources(texts());
        const auto rules = game();
        auto state = popup();

        {
            engine::SequencePlayback playback(resources.service.snapshot());
            statsui::FutureImmunityTextPlayback owner;
            require(!owner.sync(state, rules,
                    display::Screen2D::Portfolio, 0, nullptr, playback),
                "visible popup rejects missing font runtime");
            require(playback.commands().pendingCount() == 0 &&
                    playback.runtimeBitmaps().size() == 0,
                "missing font fails before queue or surface mutation");
        }

        fonts::Runtime font;
        configureFont(font);

        {
            engine::SequencePlayback playback(resources.service.snapshot());
            statsui::FutureImmunityTextPlayback owner;
            state.player = 5;
            require(!owner.sync(state, rules,
                    display::Screen2D::Portfolio, 0, &font, playback),
                "popup rejects player outside current game");
            require(playback.commands().pendingCount() == 0 &&
                    playback.runtimeBitmaps().size() == 0,
                "invalid player is rejected transactionally");
            state.player = 0;
        }

        {
            engine::SequencePlayback playback(resources.service.snapshot());
            statsui::FutureImmunityTextPlayback owner;
            state.rows.front().square = -1;
            require(!owner.sync(state, rules,
                    display::Screen2D::Portfolio, 0, &font, playback),
                "popup rejects invalid square row");
            require(playback.commands().pendingCount() == 0 &&
                    playback.runtimeBitmaps().size() == 0,
                "invalid square fails before publication");
        }
    }
}

int main()
{
    try
    {
        testVisibleAndRefresh();
        testFailures();
        std::cout << "Stats Future/Immunity text playback tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
