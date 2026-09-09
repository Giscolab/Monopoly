#include "AITradeUtility.hpp"

#include <algorithm>

namespace monopoly::ai::trade
{
    namespace
    {
        [[nodiscard]] MonopolyTradeGroup findRecursive(
            rules::PlayerNumber player,
            rules::board::PropertySet monopoly,
            std::array<rules::PlayerNumber, rules::MaxPlayers> list,
            std::size_t count,
            const PropertySets& properties) noexcept
        {
            if (count == 0)
                return {};

            auto combined = properties[player];
            for (std::size_t index = 0; index < count; ++index)
                combined |= properties[list[index]];

            if ((combined & monopoly) != monopoly)
                return {};

            MonopolyTradeGroup best{};
            if (ai::monopoliesInSet(combined) >=
                static_cast<int>(count + 1))
            {
                best.count = count;
                std::copy_n(list.begin(), count, best.players.begin());
            }

            for (std::size_t index = 0; index < count; ++index)
            {
                std::swap(list[count - 1], list[index]);
                const auto current = findRecursive(
                    player, monopoly, list, count - 1, properties);
                if (current.count != 0 &&
                    (best.count == 0 || current.count < best.count))
                {
                    best = current;
                }
                std::swap(list[index], list[count - 1]);
            }

            return best;
        }
    }

    MonopolyTradeGroup findSmallestMonopolyTrade(
        rules::PlayerNumber player,
        rules::board::PropertySet monopoly,
        std::span<const rules::PlayerNumber> candidates,
        const PropertySets& properties) noexcept
    {
        if (player >= rules::MaxPlayers ||
            candidates.size() > rules::MaxPlayers)
            return {};

        std::array<rules::PlayerNumber, rules::MaxPlayers> list{};
        for (std::size_t index = 0; index < candidates.size(); ++index)
        {
            if (candidates[index] >= rules::MaxPlayers)
                return {};
            list[index] = candidates[index];
        }

        return findRecursive(
            player,
            monopoly,
            list,
            candidates.size(),
            properties);
    }

    std::int64_t transferTax(
        const rules::GameState& state,
        rules::board::PropertySet properties) noexcept
    {
        std::int64_t tax{};
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(rules::board::SquareType::InJail);
             ++index)
        {
            const auto square = static_cast<rules::board::SquareType>(index);
            const auto bit = rules::board::propertyBit(square);
            if (bit == 0 || (properties & bit) == 0 ||
                !state.squares[index].mortgaged)
                continue;
            tax += static_cast<std::int64_t>(
                rules::board::definition(square).purchaseCost) *
                state.options.taxRate / 100;
        }
        return tax;
    }

    int vetoMonopolies(rules::board::PropertySet properties) noexcept
    {
        int count{};
        for (std::size_t group = 1;
             group < ai::ExpensiveMonopolySquares.size(); ++group)
        {
            if ((ai::monopolySet(ai::ExpensiveMonopolySquares[group]) &
                 properties) != 0)
                ++count;
        }
        return count;
    }

    int possibleMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber owner,
        rules::board::SquareType square) noexcept
    {
        if (owner >= rules::MaxPlayers)
            return -1;
        const auto group = rules::board::definition(square).group;
        if (static_cast<std::size_t>(group) >=
            rules::board::MaxPropertyGroups)
            return -1;
        const auto range = ai::groupRange(group);
        int unowned{};
        for (std::size_t index = range.begin; index < range.endExclusive; ++index)
        {
            const auto candidate = static_cast<rules::board::SquareType>(index);
            if (rules::board::definition(candidate).group != group)
                continue;
            const auto candidateOwner = state.squares[index].owner;
            if (candidateOwner == owner)
                continue;
            if (candidateOwner != rules::NobodyPlayer)
                return -1;
            ++unowned;
        }
        return unowned;
    }

}
