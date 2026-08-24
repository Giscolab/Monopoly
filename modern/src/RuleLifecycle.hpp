#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

namespace monopoly::rules::lifecycle
{
    bool onlyOneSurvivor(
        const GameState& state
    );

    bool shouldEndGame(
        const GameState& state
    );

    void declareWinner(
        GameState& state
    );

    void actionPauseGame(
        GameState& state,
        const actions::Message& message
    );

    void actionDisconnectedPlayer(
        GameState& state,
        const actions::Message& message
    );

    void recordPlayerActivity(
        GameState& state,
        PlayerNumber player
    );

    void onIdleTick(
        GameState& state
    );

    bool restartLifecyclePhase(
        GameState& state
    );
}
