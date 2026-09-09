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
}
