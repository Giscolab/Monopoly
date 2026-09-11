#pragma once

#include "AICounterTradeRuntime.hpp"

#include <array>

namespace monopoly::ai::trade
{
    struct TradeIngressState
    {
        TradeProposalList currentTrade{};
        FutureImmunityList immunities{};
        std::array<decision::CounterProposalSession,
            rules::MaxPlayers> counterSessions{};
        CounterTradeRuntimeState counterRuntime{};
        rules::PlayerNumber proposedPlayer = rules::NobodyPlayer;
        rules::PlayerNumber lastEditor = rules::NobodyPlayer;
        bool tradeStarted{};
        bool tradeOfferedForAcceptance{};
    };

    void resetTradeIngress(TradeIngressState& state) noexcept;

    [[nodiscard]] bool processTradeRuleMessage(
        const rules::GameState& gameState,
        const actions::Message& message,
        TradeIngressState& state) noexcept;

    [[nodiscard]] CounterTradeRunResult counterProposeCurrentTrade(
        const rules::GameState& gameState,
        rules::PlayerNumber player,
        bool tradeAccept,
        double counterRoll,
        int pendingActions,
        bool auctionOn,
        const decision::CounterProposalPreflightConfig& preflightConfig,
        const decision::CounterProposalBalanceConfig& balanceConfig,
        TradeIngressState& state) noexcept;
}
