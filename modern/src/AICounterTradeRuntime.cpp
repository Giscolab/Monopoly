#include "AICounterTradeRuntime.hpp"

#include "Messaging.hpp"

namespace monopoly::ai::trade
{
    CounterTradeRunResult counterProposeTrade(
        const rules::GameState& state,
        rules::PlayerNumber player,
        const TradeProposalList& currentTrade,
        const decision::CounterProposalPreflightInputs& inputs,
        const decision::CounterProposalPreflightConfig& preflightConfig,
        const decision::CounterProposalBalanceConfig& balanceConfig,
        decision::CounterProposalSession& session,
        CounterTradeRuntimeState& runtime,
        std::span<const FutureImmunityRecord> immunities) noexcept
    {
        CounterTradeRunResult result{};
        if (player >= state.numberOfPlayers ||
            inputs.proposedPlayer >= state.numberOfPlayers)
            return result;

        auto effectiveInputs = inputs;
        effectiveInputs.playerSendingTrade = effectiveInputs.playerSendingTrade ||
            runtime.sending.state != SendingTradeState::Nothing;
        const auto preflight = decision::counterProposalPreflight(
            state, player, currentTrade, effectiveInputs,
            preflightConfig, session, immunities);
        result.preflightStatus = preflight.status;

        if (preflight.status == decision::CounterProposalStatus::NotInvolved)
        {
            const bool queued = messaging::sendAction(
                actions::Type::TradeEditingDone,
                player,
                rules::BankPlayer,
                1,
                inputs.proposedPlayer);
            result.status = queued
                ? CounterTradeRunStatus::ReturnedNotInvolved
                : CounterTradeRunStatus::SendFailed;
            return result;
        }
        if (preflight.status != decision::CounterProposalStatus::Ready)
        {
            result.status = CounterTradeRunStatus::PreflightStopped;
            return result;
        }

        auto proposal = currentTrade;
        const auto balance = decision::counterProposalBalance(
            state, player, preflight, proposal,
            balanceConfig, immunities);
        result.balanceStatus = balance.status;
        if (balance.status != decision::CounterProposalBalanceStatus::Ready)
        {
            result.status = CounterTradeRunStatus::BalanceStopped;
            return result;
        }

        runtime.pendingProposal = proposal;
        runtime.player = player;
        runtime.proposedPlayer = inputs.proposedPlayer;
        runtime.hasPendingProposal = true;
        if (!inputs.tradeAccept)
            runtime.sending.sendTradeOffer = true;

        if (!sendProposedTrade(
                state, player, runtime.pendingProposal,
                inputs.tradeAccept, inputs.proposedPlayer, runtime.sending))
        {
            result.status = CounterTradeRunStatus::SendFailed;
            return result;
        }

        ++session.timesCounteredTrade;
        result.status = CounterTradeRunStatus::SendStarted;
        return result;
    }

    bool continueCounterProposalTradeSend(
        const rules::GameState& state,
        CounterTradeRuntimeState& runtime) noexcept
    {
        if (!runtime.hasPendingProposal ||
            runtime.sending.state != SendingTradeState::TradeItems ||
            runtime.player >= state.numberOfPlayers)
            return false;

        return sendProposedTrade(
            state,
            runtime.player,
            runtime.pendingProposal,
            false,
            runtime.proposedPlayer,
            runtime.sending);
    }

    void processCounterProposalActionCompleted(
        CounterTradeRuntimeState& runtime,
        actions::Type action,
        bool success) noexcept
    {
        processTradeSendActionCompleted(runtime.sending, action, success);
        if (action == actions::Type::TradeEditingDone)
            runtime.hasPendingProposal = false;
    }
}
