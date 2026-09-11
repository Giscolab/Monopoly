#pragma once

#include "AICounterTradeRuntime.hpp"
#include "AIProfile.hpp"

#include <array>
#include <cstdint>

namespace monopoly::ai::trade
{
    struct TradeTurnState
    {
        std::int64_t turnsAfterForgettingLast{};
        // Retail Ai.h: AI_MAX_TRADES is 20, independent of configured maxTrades.
        std::array<std::int64_t, 20> timeLastTrade{};
    };

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
        std::array<TradeTurnState, rules::MaxPlayers> turnState{};
        bool tradeOfferedForAcceptance{};
        bool tradeJustRejectedCountered{};
        rules::PlayerNumber playerJustRejectedCountered = rules::NobodyPlayer;
        std::uint32_t pendingTradeAcceptPlayers{};
        std::uint32_t deferredAcceptancePlayers{};
    };

    void resetTradeIngress(TradeIngressState& state) noexcept;

    [[nodiscard]] bool processTradeRuleMessage(
        const rules::GameState& gameState,
        const actions::Message& message,
        TradeIngressState& state) noexcept;

    void advanceTradeTurn(
        std::uint8_t numberOfPlayers,
        profile::ProfileSet& profiles,
        const std::array<bool, rules::MaxPlayers>& localAIPlayers,
        TradeIngressState& state) noexcept;

    [[nodiscard]] std::array<double, rules::MaxPlayers> tradeResponseAttitudeChanges(
        const rules::GameState& gameState,
        rules::PlayerNumber proposer,
        rules::PlayerNumber respondingPlayer,
        bool accepted,
        const TradeIngressState& state,
        const profile::ProfileSet& profiles,
        const profile::ConfigContext& context) noexcept;

    enum class TradeAcceptanceStatus : std::uint8_t
    {
        InvalidInput = 0,
        Suppressed,
        Busy,
        Accept,
        AcceptUninvolved,
        RejectFutureOrImmunity,
        RejectFedUp,
        CounterOrReject
    };

    struct TradeAcceptanceConfig
    {
        decision::TradeEvaluationConfig evaluation{};
        int numberTimesAllowPropertyTrade{};
        double minEvaluationThreshold{};
        double minEvaluationIfFedUp{};
    };

    struct TradeAcceptanceResult
    {
        TradeAcceptanceStatus status = TradeAcceptanceStatus::InvalidInput;
        double evaluation{};
        bool fedUp{};
    };

    [[nodiscard]] TradeAcceptanceResult evaluateCurrentTradeAcceptance(
        const rules::GameState& gameState,
        rules::PlayerNumber player,
        const TradeAcceptanceConfig& config,
        const TradeIngressState& state) noexcept;

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
