#pragma once

#include "AITradeUtility.hpp"

namespace monopoly::ai::trade
{
    enum class SendingTradeState : std::uint8_t
    {
        Nothing = 0,
        AskingTradeEdit,
        ClearingTrade,
        TradeItems,
        TradeSent
    };

    struct TradeSendRuntimeState
    {
        SendingTradeState state = SendingTradeState::Nothing;
        bool sendTradeOffer{};
    };

    [[nodiscard]] bool sendProposedTrade(
        const rules::GameState& state,
        rules::PlayerNumber player,
        const TradeProposalList& proposals,
        bool tradeAccept,
        rules::PlayerNumber proposedPlayer,
        TradeSendRuntimeState& runtime) noexcept;

    void processTradeSendActionCompleted(
        TradeSendRuntimeState& runtime,
        actions::Type action,
        bool success) noexcept;
}
