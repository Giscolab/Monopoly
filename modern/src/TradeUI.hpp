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

    inline constexpr Rect CashTradeAT1{19, 395, 52, 425};
    inline constexpr Rect CashTradeAT2{9, 425, 61, 440};
    inline constexpr Rect CashTradeBT1{619, 395, 652, 425};
    inline constexpr Rect CashTradeBT2{609, 425, 661, 440};
    inline constexpr Rect CashTradeAM1{219, 358, 252, 388};
    inline constexpr Rect CashTradeAM2{209, 388, 261, 403};
    inline constexpr Rect CashTradeBM1{419, 358, 452, 388};
    inline constexpr Rect CashTradeBM2{409, 388, 461, 403};
    inline constexpr Rect ProposeRect{202, 420, 303, 450};
    inline constexpr Rect CancelRect{306, 420, 407, 450};

    inline constexpr std::array<Rect, 4> ChanceJailRects{{
        {66, 395, 99, 414}, {666, 395, 699, 414},
        {266, 358, 299, 377}, {466, 358, 499, 377}}};
    inline constexpr std::array<Rect, 4> CommunityJailRects{{
        {66, 420, 99, 439}, {666, 420, 699, 439},
        {266, 383, 299, 402}, {466, 383, 499, 402}}};
    inline constexpr Rect FutureTradeAT{104, 398, 165, 413};
    inline constexpr Rect FutureTradeBT{704, 398, 765, 413};
    inline constexpr Rect FutureTradeAM{304, 361, 365, 376};
    inline constexpr Rect FutureTradeBM{504, 361, 565, 376};
    inline constexpr Rect ImmunityTradeAT{104, 422, 165, 437};
    inline constexpr Rect ImmunityTradeBT{704, 422, 765, 437};
    inline constexpr Rect ImmunityTradeAM{304, 385, 365, 400};
    inline constexpr Rect ImmunityTradeBM{504, 385, 565, 400};
    inline constexpr Rect FutureNewRect{607, 197, 701, 220};
    inline constexpr Rect ImmunityNewRect{704, 197, 798, 220};
    inline constexpr Rect ContractOkayRect{660, 187, 740, 209};
    inline constexpr Rect ContractUpRect{767, 115, 785, 141};
    inline constexpr Rect ContractDownRect{767, 155, 785, 181};
    inline constexpr std::array<Rect, 5> ContractListRects{{
        {614,114,764,128}, {614,128,764,142}, {614,142,764,156},
        {614,156,764,170}, {614,170,764,184}}};

    struct ContractListEntry
    {
        std::int32_t hitCount{};
        std::uint32_t properties{};
        bool selected{};
    };

    struct PropertyMoveRequest
    {
        int square{-1};
        std::uint8_t fromBox{};
        std::uint8_t toBox{};
        bool mortgaged{};
        Rect from{};
        Rect to{};

        bool operator==(const PropertyMoveRequest&) const = default;
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
        bool cashDialogVisible{};
        std::uint8_t cashDialogSide{};
        std::int64_t cashTradeAmount{};
        std::array<std::int64_t, 2> cashOriginalOffers{};
        bool contractDialogVisible{};
        rules::TradeItemKind contractDialogKind{rules::TradeItemKind::FutureRent};
        std::uint8_t contractDialogMode{};
        std::uint8_t contractDialogSide{};
        std::uint32_t contractProperties{};
        std::int32_t contractAmount{};
        int contractListOffset{};
        std::vector<ContractListEntry> contractList;
        std::optional<PropertyMoveRequest> propertyMove;
        std::vector<actions::Message> items;
    };

    using PropertyMask = std::uint32_t;

    struct PropertyProjection
    {
        std::array<PropertyMask, 2> before{};
        std::array<PropertyMask, 2> beforeMortgaged{};
        std::array<PropertyMask, 2> offered{};
        std::array<PropertyMask, 2> offeredMortgaged{};
        std::array<PropertyMask, 2> after{};
        std::array<PropertyMask, 2> afterMortgaged{};
        std::array<std::array<Rect, rules::SquareCount>, 4> hitRects{};
        std::array<std::array<int, rules::SquareCount>, 4> priorities{};
    };

    struct InputUpdate
    {
        bool consumed{};
        std::optional<display::Screen2D> requestedBackdrop;
        std::vector<actions::Message> outgoing;
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

    void refreshContractProjection(
        State& state,
        const rules::GameState& gameState) noexcept;

    [[nodiscard]] PropertyProjection projectProperties(
        const State& state,
        const rules::GameState& gameState) noexcept;

    [[nodiscard]] std::optional<int> propertyHit(
        const PropertyProjection& projection,
        int x,
        int y) noexcept;

    [[nodiscard]] InputUpdate processInput(
        State& state,
        const rules::GameState& gameState,
        display::Screen2D desiredView,
        const uimsg::Message& message);

    [[nodiscard]] std::vector<actions::Message> planEditorSubmission(
        const State& state,
        rules::PlayerNumber editor,
        std::uint32_t localHumanMask);

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
