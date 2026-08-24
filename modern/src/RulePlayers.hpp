#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

namespace monopoly::rules::players
{
    void actionNamePlayer(
        GameState& state,
        const actions::Message& message
    );
}
