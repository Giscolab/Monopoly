#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

namespace monopoly::rules::sync
{
    void resetWaitGate();


    bool beginWaitOnce(
        GameState& state,
        actions::Type hint
    );


    void waitForEverybodyReady(
        GameState& state,
        actions::Type hint
    );


    void actionIAmHere(
        GameState& state,
        const actions::Message& message
    );


    bool restartSyncPhase(
        GameState& state
    );


    void onIdleTick(
        GameState& state
    );
}
