#pragma once

#include "RuleTypes.hpp"

namespace monopoly::rules::options
{
    void setDefaults(
        GameOptions& options
    );

    // UDPsel.cpp::udpsel_SetStandardRules. Unlike ActionNewGame defaults,
    // this overwrites only the fields touched by the setup-screen preset.
    void setStandardMonopolyRules(
        GameOptions& options
    );

    void validate(
        GameOptions& options
    );
}
