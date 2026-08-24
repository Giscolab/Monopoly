#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

namespace monopoly::rules::jail
{
    void actionExitJailDecision(
        GameState& state,
        const actions::Message& message
    );
}
