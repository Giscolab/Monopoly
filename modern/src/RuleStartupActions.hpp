#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

namespace monopoly::rules::ruleactions
{
    void afterNewGame(
        GameState& state,
        PlayerNumber initiator
    );

    void restartPhase(
        GameState& state,
        const actions::Message& message
    );

    void namePlayer(
        GameState& state,
        const actions::Message& message
    );
}
