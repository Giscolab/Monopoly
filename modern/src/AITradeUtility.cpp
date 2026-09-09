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
}
