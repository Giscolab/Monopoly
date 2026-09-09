#include "AIUtility.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace monopoly::ai
{
    namespace
    {
        using rules::board::SquareGroup;
        using rules::board::SquareType;

        constexpr std::array<GroupRange, rules::board::MaxPropertyGroups>
            GroupRanges{{
                {1, 4}, {6, 10}, {11, 15}, {16, 20}, {21, 25},
                {26, 30}, {31, 35}, {37, 40}, {5, 36}, {12, 29}}};

        [[nodiscard]] constexpr std::size_t indexOf(
            SquareType square) noexcept
        {
            return static_cast<std::size_t>(square);
        }

        [[nodiscard]] constexpr SquareType squareAt(
            std::size_t index) noexcept
        {
            return static_cast<SquareType>(index);
        }
    }

    GroupRange groupRange(SquareGroup group) noexcept
    {
        const auto index = static_cast<std::size_t>(group);
        if (index >= GroupRanges.size())
            return {};
        return GroupRanges[index];
    }

    rules::board::PropertySet propertiesOwnedByPlayer(
        const rules::GameState& state,
        rules::PlayerNumber player) noexcept
    {
        rules::board::PropertySet properties{};
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(SquareType::InJail); ++index)
        {
            if (state.squares[index].owner == player)
                properties |= rules::board::propertyBit(squareAt(index));
        }
        return properties;
    }

    rules::board::PropertySet monopolySet(SquareType square) noexcept
    {
        const auto group = rules::board::definition(square).group;
        const auto range = groupRange(group);
        rules::board::PropertySet result{};
        for (std::size_t index = range.begin; index < range.endExclusive; ++index)
        {
            const auto candidate = squareAt(index);
            if (rules::board::definition(candidate).group == group)
                result |= rules::board::propertyBit(candidate);
        }
        return result;
    }

    int propertyCount(rules::board::PropertySet properties) noexcept
    {
        int count{};
        while (properties != 0)
        {
            if ((properties & 1U) != 0)
                ++count;
            properties >>= 1U;
        }
        return count;
    }

    bool testForMonopoly(
        rules::board::PropertySet properties,
        SquareType square) noexcept
    {
        const auto group = rules::board::definition(square).group;
        const auto range = groupRange(group);
        if (range.begin == range.endExclusive)
            return false;
        for (std::size_t index = range.begin; index < range.endExclusive; ++index)
        {
            const auto candidate = squareAt(index);
            if (rules::board::definition(candidate).group != group)
                continue;
            if ((properties & rules::board::propertyBit(candidate)) == 0)
                return false;
        }
        return true;
    }

    MonopolyLots monopolyLots(SquareType square) noexcept
    {
        MonopolyLots result{};
        const auto group = rules::board::definition(square).group;
        const auto range = groupRange(group);
        for (std::size_t index = range.begin; index < range.endExclusive; ++index)
        {
            const auto candidate = squareAt(index);
            if (rules::board::definition(candidate).group == group &&
                result.count < result.squares.size())
            {
                result.squares[result.count++] = candidate;
            }
        }

        for (std::size_t index = 0; index < result.count; ++index)
        {
            if (rules::board::definition(result.squares[index]).rent[0] >
                rules::board::definition(result.squares[0]).rent[0])
                std::swap(result.squares[index], result.squares[0]);
        }
        return result;
    }

    int housesOnMonopoly(
        const rules::GameState& state,
        SquareType square) noexcept
    {
        const auto group = rules::board::definition(square).group;
        const auto range = groupRange(group);
        int houses{};
        for (std::size_t index = range.begin; index < range.endExclusive; ++index)
        {
            const auto candidate = squareAt(index);
            if (rules::board::definition(candidate).group == group)
                houses += state.squares[index].houses;
        }
        return houses;
    }

    bool isMonopoly(
        const rules::GameState& state,
        SquareType square) noexcept
    {
        const auto group = rules::board::definition(square).group;
        if (static_cast<std::size_t>(group) >= rules::board::MaxPropertyGroups)
            return false;

        const auto owner = state.squares[indexOf(square)].owner;
        if (owner == rules::NobodyPlayer)
            return false;
        const auto range = groupRange(group);
        for (std::size_t index = range.begin; index < range.endExclusive; ++index)
        {
            const auto candidate = squareAt(index);
            if (rules::board::definition(candidate).group == group &&
                state.squares[index].owner != owner)
            {
                return false;
            }
        }
        return true;
    }

    bool ownsMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player,
        SquareType square,
        bool mortgageCounts) noexcept
    {
        const auto group = rules::board::definition(square).group;
        if (group == SquareGroup::Railroad || group == SquareGroup::Utility)
            return false;

        const auto owned = propertiesOwnedByPlayer(state, player);
        if (!mortgageCounts)
        {
            const auto set = monopolySet(square);
            return set != 0 && (owned & set) == set;
        }
        const auto range = groupRange(group);
        for (std::size_t index = range.begin; index < range.endExclusive; ++index)
        {
            const auto candidate = squareAt(index);
            if (rules::board::definition(candidate).group != group)
                continue;
            if ((owned & rules::board::propertyBit(candidate)) == 0 ||
                state.squares[index].mortgaged)
            {
                return false;
            }
        }
        return true;
    }

    bool playerOwnsMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player,
        bool countBaltic) noexcept
    {
        // Preserve the retail loop exactly: TRUE starts at index 1,
        // despite the historical parameter/comment naming.
        const std::size_t start = countBaltic ? 1U : 0U;
        for (std::size_t index = start; index < ExpensiveMonopolySquares.size(); ++index)
        {
            const auto square = ExpensiveMonopolySquares[index];
            if (isMonopoly(state, square) &&
                state.squares[indexOf(square)].owner == player)
                return true;
        }
        return false;
    }

    bool anyMonopoly(const rules::GameState& state) noexcept
    {
        for (const auto square : ExpensiveMonopolySquares)
        {
            if (isMonopoly(state, square))
                return true;
        }
        return false;
    }

    bool ownsPropertyFromMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player,
        SquareType square) noexcept
    {
        return (propertiesOwnedByPlayer(state, player) & monopolySet(square)) != 0;
    }

    rules::PlayerNumber firstOwnerInMonopoly(
        const rules::GameState& state,
        SquareType square) noexcept
    {
        const auto group = rules::board::definition(square).group;
        const auto range = groupRange(group);
        for (std::size_t index = range.begin; index < range.endExclusive; ++index)
        {
            const auto candidate = squareAt(index);
            if (rules::board::definition(candidate).group == group &&
                state.squares[index].owner != rules::NobodyPlayer)
                return state.squares[index].owner;
        }
        return rules::NobodyPlayer;
    }

    int numberRailroadsUtilitiesOwned(
        const rules::GameState& state,
        rules::PlayerNumber player,
        SquareGroup group,
        bool mortgageCounts,
        rules::board::PropertySet propertiesOwned) noexcept
    {
        (void)player;
        if (group != SquareGroup::Railroad && group != SquareGroup::Utility)
            return 0;

        const auto range = groupRange(group);
        int owned{};
        for (std::size_t index = range.begin; index < range.endExclusive; ++index)
        {
            const auto square = squareAt(index);
            if (rules::board::definition(square).group != group)
                continue;
            if ((propertiesOwned & rules::board::propertyBit(square)) == 0)
                continue;
            if (!mortgageCounts || !state.squares[index].mortgaged)
                ++owned;
        }
        return owned;
    }
    std::int64_t rentIfSteppedOn(
        const rules::GameState& state,
        SquareType square,
        rules::board::PropertySet propertiesOwned) noexcept
    {
        const auto index = indexOf(square);
        const auto& squareState = state.squares[index];
        const auto& definition = rules::board::definition(square);
        if (squareState.mortgaged || squareState.owner == rules::NobodyPlayer)
            return 0;

        if (ownsMonopoly(state, squareState.owner, square,
                state.options.mortgagedCountsInGroupRent))
        {
            if (squareState.houses == 0)
                return definition.rent[0] * 2;
            return definition.rent[squareState.houses];
        }

        const int count = numberRailroadsUtilitiesOwned(
            state, squareState.owner, definition.group,
            state.options.mortgagedCountsInGroupRent, propertiesOwned);
        if (count > 0)
        {
            if (definition.group == SquareGroup::Utility)
                return count == 2 ? 70 : 28;
            if (definition.group == SquareGroup::Railroad)
                return definition.rent[static_cast<std::size_t>(count)];
        }
        return definition.rent[0];
    }

    std::uint8_t freeHouses(const rules::GameState& state) noexcept
    {
        int used{};
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(SquareType::InJail); ++index)
        {
            if (state.squares[index].houses < state.options.housesPerHotel)
                used += state.squares[index].houses;
        }
        if (state.options.maximumHouses < used)
            return 0;
        return static_cast<std::uint8_t>(state.options.maximumHouses - used);
    }

    std::uint8_t freeHotels(const rules::GameState& state) noexcept
    {
        int used{};
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(SquareType::InJail); ++index)
        {
            if (state.squares[index].houses >= state.options.housesPerHotel)
                ++used;
        }
        return static_cast<std::uint8_t>(state.options.maximumHotels - used);
    }

    bool housingShortage(
        const rules::GameState& state,
        std::uint8_t criticalLevel) noexcept
    {
        return freeHouses(state) <= criticalLevel;
    }

    std::int64_t liquidAssetsForProperties(
        const rules::GameState& state,
        rules::board::PropertySet properties,
        bool countHouses,
        bool countMonopolies) noexcept
    {
        std::int64_t total{};
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(SquareType::InJail); ++index)
        {
            const auto square = squareAt(index);
            if (state.squares[index].mortgaged ||
                (properties & rules::board::propertyBit(square)) == 0)
                continue;

            const auto& definition = rules::board::definition(square);
            if (housesOnMonopoly(state, square) == 0)
            {
                if (isMonopoly(state, square) && !countMonopolies)
                    continue;
                total += definition.mortgageCost;
            }
            else if (countHouses)
            {
                total += (definition.housePurchaseCost / 2) *
                    state.squares[index].houses;
                total += definition.mortgageCost;
            }
        }
        return total;
    }

    std::int64_t totalWorth(
        const rules::GameState& state,
        rules::PlayerNumber player) noexcept
    {
        std::int64_t total = state.players[player].cash;
        const auto owned = propertiesOwnedByPlayer(state, player);
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(SquareType::InJail); ++index)
        {
            const auto square = squareAt(index);
            if ((owned & rules::board::propertyBit(square)) == 0)
                continue;
            const auto& definition = rules::board::definition(square);
            total += definition.housePurchaseCost * state.squares[index].houses;
            total += definition.purchaseCost;
        }
        return total;
    }

    std::int64_t housesCanBuyOnMonopoly(
        const rules::GameState& state,
        SquareType square) noexcept
    {
        return static_cast<std::int64_t>(state.options.housesPerHotel) *
            propertyCount(monopolySet(square)) - housesOnMonopoly(state, square);
    }
}
