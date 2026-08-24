#pragma once

#include "RuleTypes.hpp"

namespace monopoly::rules::phases
{
    bool push(
        GameState& state,
        GamePhase phase,
        PlayerNumber fromPlayer,
        PlayerNumber toPlayer,
        std::int64_t amount
    );

    bool pop(GameState& state);

    void switchTo(
        GameState& state,
        GamePhase phase,
        PlayerNumber fromPlayer,
        PlayerNumber toPlayer,
        std::int64_t amount
    );

    const PendingPhase& current(const GameState& state);
}
