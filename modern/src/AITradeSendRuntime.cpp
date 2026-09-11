#include "AITradeSendRuntime.hpp"

#include "Messaging.hpp"

namespace monopoly::ai::trade
{
    bool sendProposedTrade(
        const rules::GameState& state,
        rules::PlayerNumber player,
        const TradeProposalList& proposals,
        bool tradeAccept,
        rules::PlayerNumber proposedPlayer,
        TradeSendRuntimeState& runtime) noexcept
    {
        ProposedTradeSendInputs inputs{};
        inputs.playerSendingTrade = runtime.state != SendingTradeState::Nothing;
        inputs.tradeAccept = tradeAccept;
        inputs.sendTradeOffer = runtime.sendTradeOffer;
        inputs.proposedPlayer = proposedPlayer;

        const auto plan = buildProposedTradeSendPlan(state, player, proposals, inputs);
        if (!plan.sendable() ||
            messaging::currentQueueSize() + plan.count > messaging::MessageQueueCapacity)
            return false;

        for (std::size_t index = 0; index < plan.count; ++index)
        {
            if (!messaging::sendAction(plan.actions[index]))
                return false;
        }

        runtime.state = plan.status == ProposedTradeSendStatus::RequestEditing
            ? SendingTradeState::AskingTradeEdit
            : SendingTradeState::TradeSent;
        return true;
    }

    void processTradeSendActionCompleted(
        TradeSendRuntimeState& runtime,
        actions::Type action,
        bool success) noexcept
    {
        if (action == actions::Type::TradeEditingDone)
        {
            runtime.state = SendingTradeState::Nothing;
            runtime.sendTradeOffer = false;
        }
        if (action == actions::Type::StartTradeEditing)
            runtime.state = success ? SendingTradeState::TradeItems
                                    : SendingTradeState::Nothing;
    }
}
