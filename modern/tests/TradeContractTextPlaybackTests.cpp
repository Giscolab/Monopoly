#include "TextRefreshProof.hpp"
#include "TradeContractTextPlayback.hpp"
#include "IBarLayout.hpp"
#include "SyntheticTextResources.hpp"

#include <algorithm>
#include <array>
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

    SyntheticTextResources::Texts textFixture()
    {
        return {
            {933, u"OK_SENTINEL"},
            {2016, u"FUTURE_HEADING"},
            {2017, u"FUTURE_MODE_0"},
            {2018, u"FUTURE_MODE_A1"},
            {2019, u"FUTURE_MODE_B1"},
            {2020, u"FUTURE_MODE_2"},
            {2021, u"Confirm ^1 turns for ^P"},
            {2022, u"FUTURE_MODE_4"},
            {2023, u"FUTURE_MODE_5"},
            {2024, u"FUTURE_MODE_6"},
            {2025, u"IMMUNITY_HEADING"},
            {2026, u"IMMUNITY_MODE_0"},
            {2027, u"IMMUNITY_MODE_A1"},
            {2028, u"IMMUNITY_MODE_B1"},
            {2029, u"IMMUNITY_MODE_2"},
            {2030, u"Confirm ^1 turns for ^P"},
            {2031, u"IMMUNITY_MODE_4"},
            {2032, u"IMMUNITY_MODE_5"},
            {2033, u"IMMUNITY_MODE_6"},
            {1002, u"Mediterranean Avenue"},
            {1004, u"Baltic Avenue"}
        };
    }
    rules::GameState gameFixture()
    {
        rules::GameState game{};
        game.numberOfPlayers = 2;
        game.players[0].name = L"Alice";
        game.players[1].name = L"Bob";
        return game;
    }

    tradeui::State contractFixture()
    {
        tradeui::State state{};
        state.playerA = 0;
        state.playerB = 1;
        state.contractDialogVisible = true;
        state.contractDialogKind = rules::TradeItemKind::FutureRent;
        state.contractDialogMode = 4;
        state.contractDialogSide = 0;
        state.contractList.push_back({
            7, ibar::layout::propertyBit(1), true});
        state.contractList.push_back({
            3, ibar::layout::propertyBit(3), false});
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

    std::size_t countColour(const data::LegacyBitmapRGBA8& image,
        std::array<std::uint8_t, 4> colour)
    {
        std::size_t count{};
        for (std::size_t offset = 0; offset + 3 < image.pixels.size(); offset += 4)
            if (image.pixels[offset] == colour[0] &&
                image.pixels[offset + 1] == colour[1] &&
                image.pixels[offset + 2] == colour[2] &&
                image.pixels[offset + 3] == colour[3])
                ++count;
        return count;
    }
    void configureFont(fonts::Runtime& font)
    {
        loadRealTestArial(font);
        require(font.saveSettings(0).has_value(),
            "save default Arial settings for contract renderer");
        require(font.saveSettings(7).has_value(),
            "save retail Trade dialog font slot 7");
    }

    void testVisiblePanelAndLiveRefresh()
    {
        SyntheticTextResources resources(textFixture());
        fonts::Runtime font;
        configureFont(font);
        const auto originalSettings = font.settings();

        engine::SequencePlayback playback(resources.service.snapshot());
        tradeui::ContractTextPlayback owner;
        auto state = contractFixture();
        const auto game = gameFixture();

        require(owner.sync(state, game, display::Screen2D::Trade,
                0, &font, playback).has_value(),
            "Future mode 4 renders the 200x225 rightpanel text surface");
        require(playback.commands().pendingCount() == 1 &&
                playback.runtimeBitmaps().size() == 1,
            "first visible contract panel allocates one runtime surface and one Start");
        require(font.settings() == originalSettings,
            "contract renderer restores caller font settings");

        require(playback.update(0).has_value(),
            "contract text Start reaches SequencePlayback");
        require(playback.world2D().size() == 1,
            "contract text owns exactly one Overlay2D root");
        const auto* object = at(playback, 600, 0);
        require(object && object->priority == tradeui::TradeContractTextPriority &&
                object->asset && object->asset->image.width == 200 &&
                object->asset->image.height == 225,
            "rightpanel is published at retail x=600 y=0 priority 148");

        const auto firstAsset = object->asset;
        require(countColour(firstAsset->image, {0, 0, 180, 255}) > 0,
            "selected list row uses retail blue COLORREF background");
        require(std::any_of(firstAsset->image.pixels.begin(),
                firstAsset->image.pixels.end(),
                [](std::uint8_t value) { return value != 0; }),
            "heading, prompt, button and list produce real pixels");

        const auto rootsBeforeRefresh = playback.runtime().roots();
        state.contractList[0].selected = false;
        state.contractList[0].hitCount = 9;
        require(textRefreshRejectsFullQueue(playback, [&] { return owner.sync(state, game, display::Screen2D::Trade, 0, &font, playback); }),
            "saturated refresh preserves old pixels and roots for a later retry");
        require(owner.sync(state, game, display::Screen2D::Trade,
                0, &font, playback).has_value(),
            "visible list mutation updates the persistent runtime bitmap in place");
        require(playback.commands().pendingCount() == 1,
            "content-only refresh does not restart the rightpanel sequence");
        const auto changedAsset = playback.runtimeBitmaps().asset(firstAsset->dataId);
        require(changedAsset && changedAsset != firstAsset,
            "content refresh keeps DataId while replacing immutable bitmap snapshot");
        require(countColour(changedAsset->image, {0, 0, 180, 255}) == 0,
            "deselecting the row clears the retail blue highlight");

        require(textRefreshPreservesRoots(playback, rootsBeforeRefresh, 1),
            "next playback tick republishes the new runtime bitmap revision");
        const auto* refreshed = at(playback, 600, 0);
        require(refreshed && refreshed->asset == changedAsset,
            "live Overlay2D consumer sees the new bitmap revision without Stop/Start");
        state.contractDialogKind = rules::TradeItemKind::Immunity;
        require(owner.sync(state, game, display::Screen2D::Trade,
                0, &font, playback).has_value(),
            "same live surface switches from Future to Immunity heading/prompt");
        const auto immunityAsset = playback.runtimeBitmaps().asset(firstAsset->dataId);
        require(immunityAsset && immunityAsset != changedAsset,
            "Future-to-Immunity text change publishes another immutable revision");
        require(playback.commands().pendingCount() == 1 &&
                textRefreshPreservesRoots(playback, rootsBeforeRefresh, 1),
            "Future-to-Immunity content change keeps the active sequence root");

        state.contractDialogKind = rules::TradeItemKind::FutureRent;
        state.contractDialogMode = 3;
        state.contractDialogSide = 1;
        state.contractAmount = 4;
        state.contractList.clear();
        require(owner.sync(state, game, display::Screen2D::Trade,
                0, &font, playback).has_value(),
            "mode 3 confirmation expands count and recipient name like FormatErrorNotification");
        require(textRefreshPreservesRoots(playback, rootsBeforeRefresh, 1),
            "confirmation text redraw preserves the same active panel before hiding");

        state.contractDialogVisible = false;
        require(owner.sync(state, game, display::Screen2D::Trade,
                0, nullptr, playback).has_value(),
            "hiding a visible contract panel needs no font runtime");
        require(playback.commands().pendingCount() == 1,
            "hiding queues exactly one Stop at priority 148");
        require(playback.update(2).has_value() && playback.world2D().size() == 0,
            "contract Stop removes the rightpanel text root");
        require(playback.runtimeBitmaps().size() == 1,
            "hidden panel retains its reusable runtime surface");
    }

    void testTransactionalFailures()
    {
        SyntheticTextResources resources(textFixture());
        const auto game = gameFixture();
        auto state = contractFixture();

        {
            engine::SequencePlayback playback(resources.service.snapshot());
            tradeui::ContractTextPlayback owner;
            require(!owner.sync(state, game, display::Screen2D::Trade,
                    0, nullptr, playback),
                "visible contract panel rejects a missing font runtime");
            require(playback.commands().pendingCount() == 0 &&
                    playback.runtimeBitmaps().size() == 0,
                "missing-font failure mutates neither FIFO nor runtime surfaces");
        }
        fonts::Runtime font;
        configureFont(font);

        {
            engine::SequencePlayback playback(resources.service.snapshot());
            tradeui::ContractTextPlayback owner;
            state.contractDialogMode = 7;
            require(!owner.sync(state, game, display::Screen2D::Trade,
                    0, &font, playback),
                "contract mode outside retail 0..6 is rejected");
            require(playback.commands().pendingCount() == 0 &&
                    playback.runtimeBitmaps().size() == 0,
                "invalid state fails before publication");
            state.contractDialogMode = 4;
        }

        {
            engine::SequencePlayback playback(resources.service.snapshot());
            bool filled = true;
            for (std::size_t index = 0;
                 index < sequence::SequenceCommandQueue::Capacity; ++index)
            {
                if (!playback.commands().enqueue(
                        sequence::StopSequenceCommand{}))
                {
                    filled = false;
                    break;
                }
            }
            require(filled && playback.commands().pendingCount() ==
                    sequence::SequenceCommandQueue::Capacity,
                "saturate contract failure-test FIFO");
            const auto before = playback.commands().pendingCount();
            tradeui::ContractTextPlayback owner;
            require(!owner.sync(state, game, display::Screen2D::Trade,
                    0, &font, playback),
                "full FIFO rejects first contract publication");
            require(playback.commands().pendingCount() == before &&
                    playback.runtimeBitmaps().size() == 0,
                "FIFO preflight rejects before rendering/allocation");
        }

        {
            engine::SequencePlayback playback(resources.service.snapshot());
            tradeui::ContractTextPlayback owner;
            state.contractDialogMode = 1;
            state.playerA = 0;
            state.playerB = 5;
            require(!owner.sync(state, game, display::Screen2D::Trade,
                    0, &font, playback),
                "contract recipient outside current player count is rejected");
            require(playback.commands().pendingCount() == 0 &&
                    playback.runtimeBitmaps().size() == 0,
                "invalid recipient is transactional");
        }
    }
}

int main()
{
    try
    {
        testVisiblePanelAndLiveRefresh();
        testTransactionalFailures();
        std::cout << "Trade contract text playback tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
