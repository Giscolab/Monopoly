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

    // Switch preserves the current snapshot, as in legacy SwitchPhase.
    void saveCurrent(GameState& state);
    bool hasCurrentSnapshot(const GameState& state);
    bool hasSnapshots(const GameState& state);
    bool currentStateChanged(const GameState& state);
    bool restoreCurrent(GameState& state);

    const PendingPhase& current(const GameState& state);
}
