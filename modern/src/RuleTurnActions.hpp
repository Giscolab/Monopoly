#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

namespace monopoly::rules::turnactions
{
    void actionStartTurn(
        GameState& state,
        const actions::Message& message
    );

    void actionEndTurn(
        GameState& state,
        const actions::Message& message
    );

    void actionRollDice(
        GameState& state,
        const actions::Message& message
    );

    void actionMoveForwards(
        GameState& state,
        const actions::Message& message
    );

    void actionMoveBackwards(
        GameState& state,
        const actions::Message& message
    );

    void actionJumpToSquare(
        GameState& state,
        const actions::Message& message
    );

    void actionLandedOnSquare(
        GameState& state,
        const actions::Message& message
    );

    // Retourne true si la phase appartient déjà à ce module.
    bool restartGameplayPhase(
        GameState& state,
        const actions::Message& message
    );

    // Partie de ActionTick() concernant GF_WAIT_START_TURN.
    void onIdleTick(
        GameState& state
    );
}

