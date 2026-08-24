#pragma once

#include "Actions.hpp"
#include "RuleArchive.hpp"
#include "RuleTypes.hpp"

namespace monopoly::rules::save
{
    void resetTransientState();
    void actionGetGameState(
        GameState& state,
        const actions::Message& message
    );

    void actionSetGameState(
        GameState& state,
        const actions::Message& message
    );

    void actionAISaveParameters(
        GameState& state,
        const actions::Message& message
    );

    void actionGetOptionsForSave(
        GameState& state,
        const actions::Message& message
    );

    void actionResyncClient(
        GameState& state,
        const actions::Message& message
    );


    bool restartSavePhase(
        GameState& state
    );


    void onIdleTick(
        GameState& state
    );
}


