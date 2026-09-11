#pragma once

#include "AIDecisionUtility.hpp"
#include "AITradeSendRuntime.hpp"

namespace monopoly::ai::trade
{
    enum class CounterTradeRunStatus : std::uint8_t
    {
        InvalidInput = 0,
        PreflightStopped,
        ReturnedNotInvolved,
        BalanceStopped,
        SendStarted,
        SendFailed
    };

    struct CounterTradeRunResult
    {
        CounterTradeRunStatus status = CounterTradeRunStatus::InvalidInput;
        decision::CounterProposalStatus preflightStatus =
            decision::CounterProposalStatus::InvalidInput;
        decision::CounterProposalBalanceStatus balanceStatus =
            decision::CounterProposalBalanceStatus::InvalidInput;

        [[nodiscard]] bool acted() const noexcept
        {
            return status == CounterTradeRunStatus::ReturnedNotInvolved ||
                status == CounterTradeRunStatus::SendStarted;
        }
    };

    struct CounterTradeRuntimeState
    {
        TradeSendRuntimeState sending{};
        TradeProposalList pendingProposal{};
        rules::PlayerNumber player = rules::NobodyPlayer;
        rules::PlayerNumber proposedPlayer = rules::NobodyPlayer;
        bool hasPendingProposal{};
    };

    [[nodiscard]] CounterTradeRunResult counterProposeTrade(
        const rules::GameState& state,
        rules::PlayerNumber player,
        const TradeProposalList& currentTrade,
        const decision::CounterProposalPreflightInputs& inputs,
        const decision::CounterProposalPreflightConfig& preflightConfig,
        const decision::CounterProposalBalanceConfig& balanceConfig,
        decision::CounterProposalSession& session,
        CounterTradeRuntimeState& runtime,
        std::span<const FutureImmunityRecord> immunities = {}) noexcept;

    [[nodiscard]] bool continueCounterProposalTradeSend(
        const rules::GameState& state,
        CounterTradeRuntimeState& runtime) noexcept;

    void processCounterProposalActionCompleted(
        CounterTradeRuntimeState& runtime,
        actions::Type action,
        bool success) noexcept;
}
