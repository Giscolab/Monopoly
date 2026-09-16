#pragma once

#include "RuleTypes.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::statsui
{
    enum class CalculatorFunction : std::uint8_t
    {
        Odds = 0,
        NetWorth,
        FutureValueToYou,
        FutureValueToOther,
        MaximumExpense,
        CurrentIncome,
        MaximumIncome,
        PotentialIncome
    };

    enum class CalculatorResultKind : std::uint8_t
    {
        Money = 0,
        Percentage
    };
    struct CalculatorSelection
    {
        int deed{-1};
        rules::PlayerNumber player{rules::NobodyPlayer};
        int turns{1};
    };

    struct CalculatorResult
    {
        CalculatorResultKind kind{CalculatorResultKind::Money};
        double value{};
    };

    [[nodiscard]] std::expected<CalculatorResult, std::string> calculate(
        CalculatorFunction function,
        const rules::GameState& state,
        const CalculatorSelection& selection) noexcept;
}
