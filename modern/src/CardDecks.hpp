#pragma once

#include "RuleTypes.hpp"

#include <random>

namespace monopoly::rules::cards
{
    void initializeDecks(
        GameState& state,
        std::mt19937& randomGenerator
    );
}
