#pragma once

#include "RuleTypes.hpp"

namespace monopoly::rules::options
{
    void setDefaults(
        GameOptions& options
    );

    void validate(
        GameOptions& options
    );
}
