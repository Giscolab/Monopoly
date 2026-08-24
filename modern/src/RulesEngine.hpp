#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

namespace monopoly::rules
{
    bool initialize();
    void shutdown();

    void process(const actions::Message& message);

    void serviceIdleTick();
    const GameState& state();
}

