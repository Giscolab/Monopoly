#pragma once

#include "LegacyTextIds.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"

#include <cstdint>

namespace monopoly::rules::rejections
{
    // Source Rule.cpp ErrorWrongPhase/ErrorWrongPlayer: rejected action,
    // targeted explanation, then one phase refresh for a non-bank sender.
    inline void send(const GameState& state, const actions::Message& message,
        std::int64_t error)
    {
        messaging::sendAction(actions::Type::NotifyActionCompleted,
            BankPlayer, AllPlayers, static_cast<std::int64_t>(message.action),
            0, message.fromPlayer, 0);
        if (message.fromPlayer == BankPlayer) return;

        const PlayerNumber destination = message.fromPlayer < MaxPlayers
            ? message.fromPlayer : AllPlayers;
        const auto phase = state.numberOfPendingPhases > 0
            ? static_cast<std::int64_t>(phases::current(state).phase) : 0;
        messaging::sendAction(actions::Type::NotifyErrorMessage,
            BankPlayer, destination, error,
            static_cast<std::int64_t>(message.action), message.fromPlayer, phase);
        messaging::sendAction(actions::Type::RestartPhase, BankPlayer, BankPlayer);
    }

    inline void wrongPhase(const GameState& state, const actions::Message& message)
    { send(state, message, legacy_text::ErrorWrongPhase); }

    inline void wrongPlayer(const GameState& state, const actions::Message& message)
    { send(state, message, legacy_text::ErrorWrongPlayer); }
}
