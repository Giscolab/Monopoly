#pragma once

#include "RuleTypes.hpp"

#include <cstdint>

namespace monopoly::rules::startingorder
{
    void recordDiceRoll(
        GameState& state,
        std::uint8_t dieA,
        std::uint8_t dieB
    );

    void restart(
        GameState& state
    );
}
