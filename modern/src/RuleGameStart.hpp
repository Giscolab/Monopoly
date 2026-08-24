#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

namespace monopoly::rules::gamestart
{
    void actionStartGame(
        GameState& state,
        const actions::Message& message
    );

    void actionAcceptConfiguration(
        GameState& state,
        const actions::Message& message
    );

    void restartConfiguration(
        GameState& state
    );

    void startGameAfterOrdering(
        GameState& state
    );
}

