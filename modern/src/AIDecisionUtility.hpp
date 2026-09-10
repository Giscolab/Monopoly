#pragma once

#include "AIUtility.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace monopoly::ai::decision
{
    enum class CashStrategy : std::uint8_t
    {
        MinimumAmount = 0,
        ExpectedRentIncludingMonopolies,
        ExpectedRentExcludingMonopolies,
        HighestRent,
        MonopolyDependent,
        Count
    };

    inline constexpr std::array<double, 8> MonopolyAverageLandingFrequency{
        0.785, 0.883, 0.957, 1.103, 1.093, 1.003, 0.957, 0.890};

    [[nodiscard]] std::int64_t excessCashAvailable(
        const rules::GameState& state,
        rules::PlayerNumber player,
        bool cashOnly,
        CashStrategy strategy,
        std::int64_t minCashOnHand,
        std::int64_t moneyOwed = 0) noexcept;

    [[nodiscard]] bool hypotheticalBuyHouse(
        rules::GameState& state,
        rules::PlayerNumber player,
        std::int64_t moneyOwed = 0) noexcept;

    inline constexpr std::uint8_t CriticalHousingLevel = 3;

    [[nodiscard]] rules::board::SquareType hypotheticalUnmortgageMonopolyProperty(
        rules::GameState& state,
        rules::PlayerNumber player,
        CashStrategy strategy,
        std::int64_t minCashOnHand,
        std::int64_t moneyOwed = 0) noexcept;

    [[nodiscard]] bool shouldGiveAwayMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player,
        CashStrategy strategy,
        std::int64_t minCashOnHand,
        int maxHousesPerSquareForGiveAway,
        std::span<const std::int64_t> moneyOwed = {}) noexcept;
}
