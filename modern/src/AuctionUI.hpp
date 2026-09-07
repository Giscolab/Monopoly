#pragma once

#include "Actions.hpp"
#include "Display.hpp"
#include "RuleTypes.hpp"
#include "UIMessages.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace monopoly::auctionui
{
    inline constexpr std::size_t BillCount = 7;
    inline constexpr std::array<std::int64_t, BillCount> BillDenominations{{
        500, 100, 50, 20, 10, 5, 1
    }};

    inline constexpr int BackdropSmallWidth = 134;
    inline constexpr int BackdropBigWidth = 200;
    inline constexpr int BillTrayWidth = 131;
    inline constexpr int BillWidth = 18;
    inline constexpr int BillHeight = 31;
    inline constexpr int BackdropHeight = 90;
    inline constexpr int BillTraysHeight = 37;
    inline constexpr int BackdropY = 600 - BackdropHeight;
    inline constexpr int BillTraysY = BackdropY - BillTraysHeight;

    enum class PennyBagsState : std::uint8_t
    {
        None = 0,
        Idle,
        Intro,
        Instructions,
        StartBidding,
        Begin,
        NameHighestBidder,
        GoingOnce,
        GoingTwice,
        Sold,
        Congrats,
        EndAuction
    };

    struct State
    {
        std::int64_t highestBid{};
        std::uint32_t playersAllowedInAuction{};
        std::array<std::int64_t, rules::MaxPlayers> bids{};
        std::optional<rules::PlayerNumber> highestBidder;
        std::uint32_t playersAllowedToBid{};
        int propertyForSale{-1};
        std::array<int, rules::MaxPlayers> backdropCenterX{};
        int backdropWidth{BackdropSmallWidth};
        display::Screen2D formerView{display::Screen2D::Invalid};
        PennyBagsState nextPennyBags{PennyBagsState::None};
        bool pennyBagsSwitch{};
        bool thirdGoingReceived{};
        std::uint32_t rollCallPlayers{};
        std::int64_t rollCallSerial{};
    };

    struct RuleUpdate
    {
        std::optional<display::Screen2D> requestedBackdrop;
        bool auctionStarted{};
    };

    struct BidRequest
    {
        rules::PlayerNumber player{rules::NobodyPlayer};
        std::int64_t amount{};
        std::uint8_t billIndex{};
    };

    void reset(State& state) noexcept;

    [[nodiscard]] RuleUpdate processRuleMessage(
        State& state,
        const rules::GameState& gameState,
        const actions::Message& message,
        display::Screen2D desiredView) noexcept;

    [[nodiscard]] bool playerPanelWanted(
        const State& state,
        rules::PlayerNumber numberOfPlayers,
        display::Screen2D desiredView,
        rules::PlayerNumber player) noexcept;

    [[nodiscard]] std::optional<BidRequest> planBid(
        const State& state,
        rules::PlayerNumber numberOfPlayers,
        display::Screen2D desiredView,
        const uimsg::Message& message,
        std::uint32_t localHumanMask) noexcept;
}
