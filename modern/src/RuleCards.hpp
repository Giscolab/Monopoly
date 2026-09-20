#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

namespace monopoly::rules::cards
{
    enum class BankPayout { Dividend50, BankError200 };
    using BankPayoutObserver = void (*)(BankPayout);
    // UI accounting is an optional observer, never a dependency of RULE.
    void setBankPayoutObserver(BankPayoutObserver observer) noexcept;
    void actionCardSeen(
        GameState& state,
        const actions::Message& message
    );
}
