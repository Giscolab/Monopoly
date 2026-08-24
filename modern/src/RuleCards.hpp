#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

namespace monopoly::rules::cards
{
    void actionCardSeen(
        GameState& state,
        const actions::Message& message
    );
}
