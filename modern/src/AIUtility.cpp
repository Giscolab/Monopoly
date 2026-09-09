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

        constexpr std::array<double, rules::SquareCount>
            LandingFrequencies{{
                1.14, 0.77, 1.0, 0.8, 0.88, 1.14, 0.86, 1.0, 0.89, 0.9,
                1.0, 1.05, 1.07, 0.87, 0.95, 1.08, 1.06, 1.0, 1.11, 1.14,
                1.10, 1.07, 1.0, 1.02, 1.19, 1.14, 1.01, 1.02, 1.04, 0.98,
                0.97, 0.99, 0.97, 1.0, 0.91, 0.88, 1.0, 0.8, 0.8, 0.98,
                0.0, 0.0}};

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
    bool playerCloseToProperty(
        const rules::GameState& state,
        rules::PlayerNumber ignorePlayer,
        SquareType property,
        std::int64_t minDistance,
        std::int64_t maxDistance) noexcept
    {
        const auto propertyIndex = static_cast<std::int64_t>(indexOf(property));
        const auto boardwalk = static_cast<std::int64_t>(indexOf(SquareType::Boardwalk));
        const auto freeParking = static_cast<std::int64_t>(indexOf(SquareType::FreeParking));
        for (rules::PlayerNumber player = 0; player < state.numberOfPlayers; ++player)
        {
            if (player == ignorePlayer)
                continue;
            const auto current = static_cast<std::int64_t>(state.players[player].currentSquare);
            if (current > boardwalk)
                continue;
            auto distance = propertyIndex - current;
            if (distance < -freeParking)
                distance += boardwalk + 1;
            if (distance >= minDistance && distance <= maxDistance)
                return true;
        }
        return false;
    }

    bool someoneCloseToMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player,
        const MonopolyLots& monopoly,
        std::int64_t minDistance,
        std::int64_t maxDistance) noexcept
    {
        for (std::size_t index = 0; index < monopoly.count; ++index)
        {
            if (playerCloseToProperty(state, player, monopoly.squares[index],
                    minDistance, maxDistance))
                return true;
        }
        return false;
    }

    std::int64_t costUnmortgageMonopoly(
        const rules::GameState& state,
        SquareType square) noexcept
    {
        const auto lots = monopolyLots(square);
        std::int64_t cost{};
        for (std::size_t index = 0; index < lots.count; ++index)
        {
            const auto lot = lots.squares[index];
            if (!state.squares[indexOf(lot)].mortgaged)
                continue;
            cost += static_cast<std::int64_t>(
                static_cast<double>(rules::board::definition(lot).mortgageCost) * 1.1);
        }
        return cost;
    }

    bool isCashCow(SquareType square) noexcept
    {
        const auto group = rules::board::definition(square).group;
        return group == SquareGroup::Utility || group == SquareGroup::Railroad;
    }

    double landingFrequency(SquareType square) noexcept
    {
        const auto index = indexOf(square);
        return index < LandingFrequencies.size() ? LandingFrequencies[index] : 0.0;
    }

    double averageRentReceived(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::int64_t startSquare,
        bool dontCountDevelopedMonopolies,
        double cashCowMultiplier,
        rules::board::PropertySet propertiesOwned) noexcept
    {
        (void)player;
        if (startSquare < 0)
            return 0.0;
        double total{};
        const auto end = static_cast<std::int64_t>(indexOf(SquareType::InJail));
        for (auto index = startSquare; index < end; ++index)
        {
            const auto square = squareAt(static_cast<std::size_t>(index));
            if ((propertiesOwned & rules::board::propertyBit(square)) == 0)
                continue;
            if (dontCountDevelopedMonopolies && housesOnMonopoly(state, square) > 0)
                continue;
            const double multiplier = isCashCow(square) ? cashCowMultiplier : 1.0;
            total += static_cast<double>(rentIfSteppedOn(state, square, propertiesOwned)) *
                landingFrequency(square) * multiplier;
        }
        return total / 7.0;
    }

    std::int64_t averageRentPaid(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::int64_t startSquare,
        bool considerDevelopedMonopolies,
        double cashCowMultiplier) noexcept
    {
        double total{};
        for (rules::PlayerNumber counter = 0; counter < state.numberOfPlayers; ++counter)
        {
            if (counter == player ||
                state.players[counter].currentSquare == indexOf(SquareType::OffBoard))
                continue;
            const auto owned = propertiesOwnedByPlayer(state, counter);
            total += averageRentReceived(state, counter, startSquare,
                !considerDevelopedMonopolies, cashCowMultiplier, owned);
        }

        if (static_cast<std::int64_t>(indexOf(SquareType::IncomeTax)) >= startSquare)
        {
            const auto worth = totalWorth(state, player);
            if (state.options.luxuryTaxAmount > 0)
            {
                const double quotient = static_cast<double>(worth) /
                    static_cast<double>(state.options.luxuryTaxAmount);
                if (quotient > static_cast<double>(state.options.flatTaxFee))
                    total += static_cast<double>(state.options.flatTaxFee) / 7.0;
                else
                    total += quotient / 7.0;
            }
        }
        if (static_cast<std::int64_t>(indexOf(SquareType::LuxuryTax)) >= startSquare)
            total += static_cast<double>(state.options.luxuryTaxAmount) / 7.0;
        return static_cast<std::int64_t>(total);
    }

    std::int64_t minimumCalculatedExpenses(
        const rules::GameState& state,
        rules::PlayerNumber player) noexcept
    {
        auto current = static_cast<std::int64_t>(state.players[player].currentSquare);
        if (current == static_cast<std::int64_t>(indexOf(SquareType::InJail)))
            current = static_cast<std::int64_t>(indexOf(SquareType::JustVisiting));
        if (current > static_cast<std::int64_t>(indexOf(SquareType::Boardwalk)))
            return -1;
        return averageRentPaid(state, player, current, false, 1.0);
    }

    std::int64_t potentialIncome(
        const rules::GameState& state,
        rules::PlayerNumber player) noexcept
    {
        std::int64_t income{};
        const auto maxRentIndex = static_cast<std::size_t>(
            std::clamp(state.options.housesPerHotel, 0,
                static_cast<int>(rules::board::RentStepCount - 1)));
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(SquareType::InJail); ++index)
        {
            if (state.squares[index].owner != player)
                continue;
            const auto square = squareAt(index);
            const auto& definition = rules::board::definition(square);
            if (isMonopoly(state, square))
            {
                if (state.squares[index].houses == 0)
                    income += definition.rent[0] * 2;
                else
                    income += definition.rent[maxRentIndex];
            }
            else
                income += definition.rent[0];
        }
        return income;
    }

    rules::PlayerNumber mostExpensivePotentialIncome(
        const rules::GameState& state) noexcept
    {
        std::int64_t highest{};
        auto highestPlayer = rules::NobodyPlayer;
        for (rules::PlayerNumber player = 0; player < state.numberOfPlayers; ++player)
        {
            const auto income = potentialIncome(state, player);
            if (income > highest)
            {
                highest = income;
                highestPlayer = player;
            }
        }
        return highestPlayer;
    }

    HighestRentResult highestRentSquare(
        const rules::GameState& state,
        rules::PlayerNumber player) noexcept
    {
        HighestRentResult result{};
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(SquareType::InJail); ++index)
        {
            const auto& squareState = state.squares[index];
            if (squareState.mortgaged || squareState.owner == player ||
                squareState.owner == rules::NobodyPlayer ||
                squareState.owner == rules::BankPlayer)
                continue;

            const auto ownerProperties =
                propertiesOwnedByPlayer(state, squareState.owner);
            const auto square = squareAt(index);
            const auto currentRent =
                rentIfSteppedOn(state, square, ownerProperties);
            if (currentRent > result.rent)
            {
                result.rent = currentRent;
                result.square = square;
            }
        }
        return result;
    }

    rules::PlayerNumber bestCurrentRent(
        const rules::GameState& state) noexcept
    {
        double highestIncome{};
        auto highestPlayer = rules::NobodyPlayer;
        for (rules::PlayerNumber player = 0;
             player < state.numberOfPlayers; ++player)
        {
            const auto owned = propertiesOwnedByPlayer(state, player);
            const auto income = averageRentReceived(
                state, player, 0, false, 1.0, owned);
            if (income > highestIncome)
            {
                highestIncome = income;
                highestPlayer = player;
            }
        }
        return highestPlayer;
    }
    int monopoliesInSet(
        rules::board::PropertySet properties) noexcept
    {
        int count{};
        for (const auto square : ExpensiveMonopolySquares)
        {
            if (testForMonopoly(properties, square))
                ++count;
        }
        return count;
    }

    int monopoliesBetweenPlayers(
        const rules::GameState& state,
        std::span<const rules::PlayerNumber> players,
        rules::board::SquareType extraProperty) noexcept
    {
        rules::board::PropertySet combined{};
        for (const auto player : players)
            combined |= propertiesOwnedByPlayer(state, player);
        combined |= rules::board::propertyBit(extraProperty);
        return monopoliesInSet(combined);
    }

    std::size_t removePlayersFromList(
        std::span<rules::PlayerNumber> players,
        std::size_t playerCount,
        std::span<const rules::PlayerNumber> remove) noexcept
    {
        playerCount = std::min(playerCount, players.size());
        for (auto removeIndex = remove.size(); removeIndex > 0; --removeIndex)
        {
            const auto target = remove[removeIndex - 1];
            for (std::size_t current = 0; current < playerCount; ++current)
            {
                if (players[current] != target)
                    continue;
                --playerCount;
                for (std::size_t index = current; index < playerCount; ++index)
                    players[index] = players[index + 1];
            }
        }
        return playerCount;
    }

    rules::board::PropertySet xorProperties(
        rules::board::PropertySet properties,
        std::span<const rules::board::SquareType> squares) noexcept
    {
        for (const auto square : squares)
        {
            if (square != SquareType::Go)
                properties ^= rules::board::propertyBit(square);
        }
        return properties;
    }

    MonopolyCollection monopoliesOwned(
        const rules::GameState& state,
        rules::PlayerNumber player,
        bool mortgageCounts) noexcept
    {
        MonopolyCollection result{};
        for (const auto square : ExpensiveMonopolySquares)
        {
            if (ownsMonopoly(state, player, square, mortgageCounts))
                result.representatives[result.count++] = square;
        }
        return result;
    }

    int propertiesLeftToBuy(const rules::GameState& state) noexcept
    {
        int count{};
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(SquareType::InJail); ++index)
        {
            if (state.squares[index].owner != rules::NobodyPlayer)
                continue;
            const auto group = rules::board::definition(squareAt(index)).group;
            if (static_cast<std::size_t>(group) <=
                static_cast<std::size_t>(SquareGroup::ParkPlace))
                ++count;
        }
        return count;
    }

    MonopolyStage monopolyStage(
        const rules::GameState& state,
        rules::PlayerNumber player) noexcept
    {
        if (anyMonopoly(state))
        {
            return monopoliesOwned(state, player, false).count != 0
                ? MonopolyStage::MonopoliesOwnOne
                : MonopolyStage::MonopoliesNotOwnOne;
        }
        return propertiesLeftToBuy(state) <= CriticalUnownedSquares
            ? MonopolyStage::NoMonopolies
            : MonopolyStage::Buying;
    }

    void testSellHouses(
        rules::GameState& state,
        const MonopolyLots& monopoly,
        SquareType testSquare) noexcept
    {
        if (monopoly.count == 0)
            return;

        const int housesPerHotel = state.options.housesPerHotel;
        int hotelsOnMonopoly{};
        int housesOnLots{};
        for (std::size_t index = 0; index < monopoly.count; ++index)
        {
            const auto houses = state.squares[indexOf(monopoly.squares[index])].houses;
            if (houses == housesPerHotel)
                ++hotelsOnMonopoly;
            else
                housesOnLots += houses;
        }

        const int totalHouseUnits =
            hotelsOnMonopoly * housesPerHotel + housesOnLots;
        int housesNeedToSell = 1;
        if (hotelsOnMonopoly > 0 &&
            freeHouses(state) < housesPerHotel - 1)
        {
            housesNeedToSell = hotelsOnMonopoly * housesPerHotel -
                freeHouses(state);
        }

        const int remaining = totalHouseUnits - housesNeedToSell;
        const int lotCount = static_cast<int>(monopoly.count);
        const int minHouses = remaining / lotCount;
        const int maxHouses = (remaining + lotCount - 1) / lotCount;
        int maxHouseLots = remaining % lotCount;

        for (std::size_t index = 0; index < monopoly.count; ++index)
        {
            const auto square = monopoly.squares[index];
            if (square == testSquare)
            {
                state.squares[indexOf(square)].houses =
                    static_cast<std::uint8_t>(minHouses);
            }
            else if (maxHouseLots > 0)
            {
                --maxHouseLots;
                state.squares[indexOf(square)].houses =
                    static_cast<std::uint8_t>(maxHouses);
            }
            else
            {
                state.squares[indexOf(square)].houses =
                    static_cast<std::uint8_t>(minHouses);
            }
        }
    }

    SquareType findLowestRentProperty(
        const rules::GameState& state,
        rules::PlayerNumber player,
        rules::board::PropertySet properties) noexcept
    {
        auto bestSquare = SquareType::Go;
        double highestRemainingRent = -1.0;
        const auto propertiesOwned = propertiesOwnedByPlayer(state, player);

        for (std::size_t index = 0;
             index < static_cast<std::size_t>(SquareType::InJail); ++index)
        {
            const auto square = squareAt(index);
            if ((properties & rules::board::propertyBit(square)) == 0 ||
                state.squares[index].mortgaged)
                continue;

            auto candidate = state;
            if (housesOnMonopoly(candidate, square) > 0)
            {
                const auto lots = monopolyLots(square);
                testSellHouses(candidate, lots, square);
            }
            else
            {
                candidate.squares[index].mortgaged = true;
            }

            const auto remainingRent = averageRentReceived(
                candidate, player, 0, false, 1.0, propertiesOwned);
            if (highestRemainingRent < 0.0 ||
                remainingRent > highestRemainingRent)
            {
                highestRemainingRent = remainingRent;
                bestSquare = square;
            }
        }
        return bestSquare;
    }

    SquareType findHighestRentMortgaged(
        const rules::GameState& state,
        rules::PlayerNumber player,
        rules::board::PropertySet properties,
        std::int64_t topUnmortgageCost) noexcept
    {
        auto bestSquare = SquareType::Go;
        double highestRent = -1.0;
        const auto propertiesOwned = propertiesOwnedByPlayer(state, player);

        for (std::size_t index = 0;
             index < static_cast<std::size_t>(SquareType::InJail); ++index)
        {
            const auto square = squareAt(index);
            if ((properties & rules::board::propertyBit(square)) == 0 ||
                !state.squares[index].mortgaged)
                continue;

            const double cost = static_cast<double>(
                rules::board::definition(square).mortgageCost) * 1.1;
            if (cost > static_cast<double>(topUnmortgageCost))
                continue;

            auto candidate = state;
            candidate.squares[index].mortgaged = false;
            const auto currentRent = averageRentReceived(
                candidate, player, 0, false, 1.0, propertiesOwned);
            if (highestRent < 0.0 || currentRent > highestRent)
            {
                highestRent = currentRent;
                bestSquare = square;
            }
        }
        return bestSquare;
    }

    TokenReaction squareMoveReaction(
        const rules::GameState& state,
        rules::PlayerNumber player,
        SquareType landSquare,
        rules::CardType card) noexcept
    {
        const bool cardSquare =
            landSquare == SquareType::CommunityChest1 ||
            landSquare == SquareType::CommunityChest2 ||
            landSquare == SquareType::CommunityChest3 ||
            landSquare == SquareType::Chance1 ||
            landSquare == SquareType::Chance2 ||
            landSquare == SquareType::Chance3 ||
            landSquare == SquareType::Go;

        if (cardSquare)
        {
            switch (card)
            {
            case rules::CardType::ChanceGet150FromBank:
                return HappyReactionRent <= 150
                    ? TokenReaction::Happy : TokenReaction::Neutral;
            case rules::CardType::ChanceGoToBoardwalk:
                return squareMoveReaction(
                    state, player, SquareType::Boardwalk);
            case rules::CardType::ChanceGoToStCharlesPlace:
                return squareMoveReaction(
                    state, player, SquareType::StCharlesPlace);
            case rules::CardType::ChancePay25EachHouse100EachHotel:
            case rules::CardType::CommunityPay40EachHouse115EachHotel:
            {
                const auto monopolies = monopoliesOwned(state, player, true);
                std::int64_t totalHouses{};
                for (std::size_t index = 0; index < monopolies.count; ++index)
                {
                    totalHouses += housesOnMonopoly(
                        state, monopolies.representatives[index]);
                }
                const auto cost = totalHouses *
                    (card == rules::CardType::ChancePay25EachHouse100EachHotel
                        ? 25 : 40);
                if (cost >= AngryReactionRent)
                    return TokenReaction::Angry;
                if (cost >= SadReactionRent)
                    return TokenReaction::Sad;
                return TokenReaction::Neutral;
            }
            case rules::CardType::ChanceGoToIllinoisAvenue:
                return squareMoveReaction(
                    state, player, SquareType::IllinoisAvenue);
            case rules::CardType::ChanceGoDirectlyToJail:
            case rules::CardType::CommunityGoDirectlyToJail:
            {
                const auto exposure = averageRentPaid(
                    state, player, 0, true, 1.0);
                return exposure > AngryReactionRent
                    ? TokenReaction::Happy : TokenReaction::Neutral;
            }
            case rules::CardType::ChanceGoBackThreeSpaces:
            {
                const auto current = static_cast<std::size_t>(landSquare);
                if (current < 3)
                    return TokenReaction::Neutral;
                const auto destination = squareAt(current - 3);
                if (destination == SquareType::CommunityChest3)
                    return TokenReaction::Neutral;
                return squareMoveReaction(state, player, destination);
            }
            case rules::CardType::CommunityGet100FromBank1:
            case rules::CardType::CommunityGet100FromBank2:
            case rules::CardType::CommunityGet100FromBank3:
                return HappyReactionRent <= 100
                    ? TokenReaction::Happy : TokenReaction::Neutral;
            case rules::CardType::CommunityGet200FromBank:
                return HappyReactionRent <= 200
                    ? TokenReaction::Happy : TokenReaction::Neutral;
            case rules::CardType::CommunityPay150ToBank:
                if (AngryReactionRent <= 150)
                    return TokenReaction::Angry;
                return SadReactionRent <= 150
                    ? TokenReaction::Sad : TokenReaction::Neutral;
            case rules::CardType::CommunityPay100ToBank:
                return SadReactionRent <= 100
                    ? TokenReaction::Sad : TokenReaction::Neutral;
            default:
                return TokenReaction::Neutral;
            }
        }

        const auto squareIndex = indexOf(landSquare);
        if (squareIndex >= state.squares.size())
            return TokenReaction::Neutral;
        const auto owner = state.squares[squareIndex].owner;
        if (owner == player)
            return TokenReaction::Neutral;

        rules::board::PropertySet ownerProperties{};
        if (owner != rules::NobodyPlayer && owner != rules::BankPlayer)
            ownerProperties = propertiesOwnedByPlayer(state, owner);
        const auto rent = rentIfSteppedOn(state, landSquare, ownerProperties);
        if (rent >= AngryReactionRent)
            return TokenReaction::Angry;
        if (rent >= SadReactionRent)
            return TokenReaction::Sad;
        return TokenReaction::Neutral;
    }
}
