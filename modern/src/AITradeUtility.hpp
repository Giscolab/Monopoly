#pragma once

#include "AIUtility.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace monopoly::ai::trade
{
    struct MonopolyTradeGroup
    {
        std::array<rules::PlayerNumber, rules::MaxPlayers> players{};
        std::size_t count{};
    };

    using PropertySets = std::array<
        rules::board::PropertySet,
        rules::MaxPlayers>;

    [[nodiscard]] MonopolyTradeGroup findSmallestMonopolyTrade(
        rules::PlayerNumber player,
        rules::board::PropertySet monopoly,
        std::span<const rules::PlayerNumber> candidates,
        const PropertySets& properties) noexcept;
    [[nodiscard]] std::int64_t transferTax(
        const rules::GameState& state,
        rules::board::PropertySet properties) noexcept;
    [[nodiscard]] int vetoMonopolies(
        rules::board::PropertySet properties) noexcept;
    [[nodiscard]] int possibleMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber owner,
        rules::board::SquareType square) noexcept;

    enum class MonopolyTradeDecision : std::uint8_t
    {
        Avoid = 0,
        Required = 1,
        Maybe = 2
    };

    [[nodiscard]] MonopolyTradeDecision shouldTradeForMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player) noexcept;

}
