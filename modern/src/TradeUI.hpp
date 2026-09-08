#pragma once

#include "Actions.hpp"
#include "Display.hpp"
#include "RuleTypes.hpp"
#include "UIMessages.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace monopoly::tradeui
{
    inline constexpr std::size_t MaxTradeMessages =
        28 + 15 + rules::MaxCountHitSets + 2;
    static_assert(MaxTradeMessages == 105);

    inline constexpr int PlayerSelectX = 306;
    inline constexpr int PlayerSelectY = 234;
    inline constexpr int PlayerSelectWidth = 188;
    inline constexpr int PlayerSelectHeight = 209;
    inline constexpr std::uint16_t PlayerSelectPriority = 205;
    inline constexpr std::uint8_t OffBoardSquare = 41;

    struct Rect
    {
        int left{};
        int top{};
        int right{};
        int bottom{};

        [[nodiscard]] constexpr bool contains(int x, int y) const noexcept
        {
            return x >= left && x < right && y >= top && y < bottom;
        }

        bool operator==(const Rect&) const = default;
    };

    struct State
    {
        rules::PlayerNumber playerA{rules::MaxPlayers};
        rules::PlayerNumber playerB{rules::MaxPlayers};
        rules::PlayerNumber tradeFrom{rules::MaxPlayers};
        bool editMode{true};
        bool playerSelectVisible{};
        bool ignoreEntryClick{};
        bool proposed{};
        bool showPropose{};
        bool aiProposing{};
        display::Screen2D formerView{display::Screen2D::Invalid};
        int desiredTradePanels{-1};
        std::array<std::int64_t, 4> cashDesired{};
        std::array<std::uint8_t, 2> jailCardDesired{};
        std::array<std::uint8_t, 2> immunityFutureDesired{};
        std::vector<actions::Message> items;
    };

    struct RuleUpdate
    {
        std::optional<display::Screen2D> requestedBackdrop;
    };

    void reset(State& state) noexcept;

    [[nodiscard]] bool beginLocalTrade(
        State& state,
        const rules::GameState& gameState,
        rules::PlayerNumber source) noexcept;

    [[nodiscard]] std::optional<Rect> playerTokenRect(
        const State& state,
        const rules::GameState& gameState,
        rules::PlayerNumber player) noexcept;

    [[nodiscard]] std::optional<rules::PlayerNumber> planPartnerSelection(
        State& state,
        const rules::GameState& gameState,
        display::Screen2D desiredView,
        const uimsg::Message& message) noexcept;

    [[nodiscard]] bool selectPartner(
        State& state,
        const rules::GameState& gameState,
        rules::PlayerNumber player) noexcept;

    [[nodiscard]] bool addTradeItem(
        State& state,
        const rules::GameState& gameState,
        const actions::Message& message);

    [[nodiscard]] bool addUiImmunity(
        rules::GameState& gameState,
        rules::PlayerNumber from,
        rules::PlayerNumber to,
        rules::CountHitType hitType,
        std::int32_t hitCount,
        std::uint32_t properties,
        bool tradingItem) noexcept;

    [[nodiscard]] RuleUpdate processRuleMessage(
        State& state,
        rules::GameState& gameState,
        const actions::Message& message,
        display::Screen2D currentView,
        std::uint32_t localHumanMask);
}
