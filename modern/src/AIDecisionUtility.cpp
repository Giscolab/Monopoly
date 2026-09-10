#include "AIDecisionUtility.hpp"

#include <algorithm>
#include <limits>

namespace monopoly::ai::decision
{
    namespace
    {
        [[nodiscard]] std::int64_t reserveForHighestDevelopedRent(
            const rules::GameState& state,
            rules::PlayerNumber player) noexcept
        {
            const auto highest = ai::highestRentSquare(state, player);
            if (highest.square == rules::board::SquareType::Go ||
                ai::housesOnMonopoly(state, highest.square) == 0)
                return 0;
            return highest.rent;
        }

        [[nodiscard]] double monopolyFrequency(
            rules::board::SquareType representative) noexcept
        {
            const auto group = rules::board::definition(representative).group;
            const auto index = static_cast<std::size_t>(group);
            return index < MonopolyAverageLandingFrequency.size()
                ? MonopolyAverageLandingFrequency[index] : 0.0;
        }
    }

    std::int64_t excessCashAvailable(
        const rules::GameState& state,
        rules::PlayerNumber player,
        bool cashOnly,
        CashStrategy strategy,
        std::int64_t minCashOnHand,
        std::int64_t moneyOwed) noexcept
    {
        if (state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers ||
            strategy >= CashStrategy::Count)
            return 0;

        auto liquid = cashOnly
            ? state.players[player].cash
            : ai::liquidAssets(state, player, false, false, moneyOwed);
        std::int64_t payments{};

        switch (strategy)
        {
        case CashStrategy::MinimumAmount:
            payments = minCashOnHand;
            break;
        case CashStrategy::ExpectedRentIncludingMonopolies:
            payments = ai::averageRentPaid(state, player, 0, true, 1.0);
            payments = std::max(payments, minCashOnHand);
            break;
        case CashStrategy::ExpectedRentExcludingMonopolies:
            payments = ai::averageRentPaid(state, player, 0, false, 1.0);
            payments = std::max(payments, minCashOnHand);
            break;
        case CashStrategy::HighestRent:
            payments = reserveForHighestDevelopedRent(state, player);
            payments += ai::averageRentPaid(state, player, 0, false, 1.0);
            payments = std::max(payments, minCashOnHand);
            break;
        case CashStrategy::MonopolyDependent:
            if (ai::anyMonopoly(state) &&
                ai::mostExpensivePotentialIncome(state) == player &&
                ai::bestCurrentRent(state) == player)
            {
                payments = reserveForHighestDevelopedRent(state, player);
            }
            payments += ai::averageRentPaid(state, player, 0, false, 1.0);
            payments = std::max(payments, minCashOnHand);
            break;
        case CashStrategy::Count:
            return 0;
        }

        return std::max<std::int64_t>(0, liquid - payments);
    }

    bool hypotheticalBuyHouse(
        rules::GameState& state,
        rules::PlayerNumber player,
        std::int64_t moneyOwed) noexcept
    {
        if (state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers)
            return false;

        const auto liquid = ai::liquidAssets(
            state, player, false, false, moneyOwed);
        if (liquid == 0)
            return false;

        const auto monopolies = ai::monopoliesOwned(state, player, true);
        auto bestSquare = rules::board::SquareType::Go;
        double bestMetric{};

        for (std::size_t index = 0; index < monopolies.count; ++index)
        {
            const auto representative = monopolies.representatives[index];
            const auto lots = ai::monopolyLots(representative);
            const auto houses = ai::housesOnMonopoly(state, representative);
            const auto toThree = static_cast<std::int64_t>(lots.count * 3) - houses;
            if (toThree < 1)
                continue;
            const auto& definition = rules::board::definition(representative);
            if (toThree * definition.housePurchaseCost > liquid)
                continue;
            if (bestMetric < static_cast<double>(definition.rent[0]))
            {
                bestMetric = static_cast<double>(definition.rent[0]);
                bestSquare = representative;
            }
        }

        if (bestSquare == rules::board::SquareType::Go)
        {
            bestMetric = 0.0;
            for (std::size_t index = 0; index < monopolies.count; ++index)
            {
                const auto representative = monopolies.representatives[index];
                const auto lots = ai::monopolyLots(representative);
                if (ai::housesOnMonopoly(state, representative) >=
                    static_cast<std::int64_t>(lots.count * 3))
                    continue;
                const auto& definition = rules::board::definition(representative);
                if (definition.housePurchaseCost > liquid)
                    continue;
                const auto frequency = monopolyFrequency(representative);
                if (bestMetric < frequency)
                {
                    bestMetric = frequency;
                    bestSquare = representative;
                }
            }
        }

        if (bestSquare == rules::board::SquareType::Go)
        {
            bestMetric = 0.0;
            for (std::size_t index = 0; index < monopolies.count; ++index)
            {
                const auto representative = monopolies.representatives[index];
                const auto lots = ai::monopolyLots(representative);
                if (ai::housesOnMonopoly(state, representative) >=
                    static_cast<std::int64_t>(lots.count * state.options.housesPerHotel))
                    continue;
                const auto& definition = rules::board::definition(representative);
                if (definition.housePurchaseCost > liquid)
                    continue;
                const auto frequency = monopolyFrequency(representative);
                if (bestMetric < frequency)
                {
                    bestMetric = frequency;
                    bestSquare = representative;
                }
            }
        }

        if (bestSquare == rules::board::SquareType::Go)
            return false;

        const auto lots = ai::monopolyLots(bestSquare);
        if (lots.count == 0)
            return false;
        auto topHousesOnLot = state.options.housesPerHotel;
        if (state.options.evenBuildRule)
        {
            const auto balancedTop = static_cast<std::uint8_t>(
                ai::housesOnMonopoly(state, bestSquare) /
                    static_cast<std::int64_t>(lots.count) + 1);
            if (balancedTop < topHousesOnLot)
                topHousesOnLot = balancedTop;
        }

        auto chosen = rules::board::SquareType::Go;
        for (std::size_t index = 0; index < lots.count; ++index)
        {
            const auto square = lots.squares[index];
            if (state.squares[static_cast<std::size_t>(square)].houses < topHousesOnLot)
            {
                chosen = square;
                break;
            }
        }
        if (chosen == rules::board::SquareType::Go)
            return false;

        ++state.squares[static_cast<std::size_t>(chosen)].houses;
        // Retail subtracts monopolies[0]'s house price here even when another
        // monopoly was selected. Debit the selected monopoly's actual house price.
        state.players[player].cash -=
            rules::board::definition(bestSquare).housePurchaseCost;
        return true;
    }
}
