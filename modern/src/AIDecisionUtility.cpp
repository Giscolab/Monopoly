#include "AIDecisionUtility.hpp"
#include "AITradeUtility.hpp"

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
    rules::board::SquareType hypotheticalUnmortgageMonopolyProperty(
        rules::GameState& state,
        rules::PlayerNumber player,
        CashStrategy strategy,
        std::int64_t minCashOnHand,
        std::int64_t moneyOwed) noexcept
    {
        using rules::board::SquareType;
        if (state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers || strategy >= CashStrategy::Count)
            return SquareType::Go;

        const auto owned = ai::propertiesOwnedByPlayer(state, player);
        const auto excess = excessCashAvailable(
            state, player, false, strategy, minCashOnHand, moneyOwed);

        const auto tryRange = [&](std::size_t begin, std::size_t endExclusive) {
            for (std::size_t index = begin; index < endExclusive; ++index)
            {
                const auto square = static_cast<SquareType>(index);
                auto& runtime = state.squares[index];
                const auto bit = rules::board::propertyBit(square);
                if (!runtime.mortgaged || bit == 0 || (owned & bit) == 0 ||
                    !ai::testForMonopoly(owned, square))
                    continue;

                const auto rawCost = static_cast<double>(
                    rules::board::definition(square).mortgageCost) * 1.1;
                // Preserve the retail comparison against the untruncated double.
                if (static_cast<double>(excess) < rawCost)
                    continue;

                runtime.mortgaged = false;
                state.players[player].cash -= static_cast<std::int64_t>(rawCost);
                return square;
            }
            return SquareType::Go;
        };

        auto square = tryRange(
            static_cast<std::size_t>(SquareType::OrientalAvenue),
            static_cast<std::size_t>(SquareType::InJail));
        if (square != SquareType::Go)
            return square;

        return tryRange(
            static_cast<std::size_t>(SquareType::Go),
            static_cast<std::size_t>(SquareType::BalticAvenue) + 1);
    }

    bool shouldUnmortgageProperty(
        const rules::GameState& state,
        rules::PlayerNumber player,
        CashStrategy strategy,
        std::int64_t minCashOnHand,
        std::int64_t moneyOwed) noexcept
    {
        using rules::board::SquareType;
        if (state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers || strategy >= CashStrategy::Count)
            return false;

        auto simulated = state;
        if (!ai::housingShortage(state, CriticalHousingLevel))
        {
            while (hypotheticalBuyHouse(simulated, player, moneyOwed))
            {
            }
        }

        const auto owned = ai::propertiesOwnedByPlayer(state, player);
        const auto cashOnly = excessCashAvailable(
            simulated, player, true, strategy, minCashOnHand, moneyOwed);
        const auto mortgageable = excessCashAvailable(
            simulated, player, false, strategy, minCashOnHand, moneyOwed);

        for (std::size_t index = 0;
             index < static_cast<std::size_t>(SquareType::InJail); ++index)
        {
            const auto square = static_cast<SquareType>(index);
            const auto bit = rules::board::propertyBit(square);
            if (bit == 0 || (owned & bit) == 0 || !state.squares[index].mortgaged)
                continue;

            const auto cost = static_cast<std::int64_t>(
                static_cast<double>(rules::board::definition(square).mortgageCost) * 1.1);
            const auto available = ai::testForMonopoly(owned, square)
                ? mortgageable : cashOnly;
            if (available >= cost)
                return true;
        }
        return false;
    }

    HousePurchaseDecision shouldBuyHouse(
        const rules::GameState& state,
        rules::PlayerNumber player,
        CashStrategy strategy,
        std::int64_t minCashOnHand,
        std::uint8_t housingPurchaseStrategy,
        std::int64_t moneyOwed) noexcept
    {
        using rules::board::SquareType;
        if (state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers || strategy >= CashStrategy::Count ||
            state.options.housesPerHotel == 0)
            return HousePurchaseDecision::No;

        const auto excess = excessCashAvailable(
            state, player, false, strategy, minCashOnHand, moneyOwed);
        if (excess == 0)
            return HousePurchaseDecision::No;

        const auto monopolies = ai::monopoliesOwned(state, player, false);
        if (monopolies.count == 0)
            return HousePurchaseDecision::No;

        const bool shortage = ai::housingShortage(state, CriticalHousingLevel);
        const auto topHousesOnLot = static_cast<std::uint8_t>(
            shortage ? state.options.housesPerHotel - 1 : state.options.housesPerHotel);
        bool foundDeferredMonopoly{};

        for (std::size_t index = 0;
             index < static_cast<std::size_t>(SquareType::InJail); ++index)
        {
            const auto square = static_cast<SquareType>(index);
            const auto& definition = rules::board::definition(square);
            bool belongsToMonopoly{};
            for (std::size_t monopolyIndex = 0; monopolyIndex < monopolies.count; ++monopolyIndex)
            {
                if (definition.group == rules::board::definition(
                        monopolies.representatives[monopolyIndex]).group)
                {
                    belongsToMonopoly = true;
                    break;
                }
            }
            if (!belongsToMonopoly || state.squares[index].houses >= topHousesOnLot)
                continue;

            if ((housingPurchaseStrategy & HouseBuyWithin12) != 0 && !shortage &&
                !ai::playerCloseToProperty(state, player, square, 2, 12))
            {
                foundDeferredMonopoly = true;
                continue;
            }

            std::int64_t buyCount = 1;
            if ((housingPurchaseStrategy & HouseBuyAtLeast3) != 0 && !shortage)
            {
                const auto lots = ai::monopolyLots(square);
                buyCount = static_cast<std::int64_t>(lots.count * 3) -
                    ai::housesOnMonopoly(state, square);
                if (buyCount < 1)
                    buyCount = 1;
            }

            const auto unmortgageCost = ai::costUnmortgageMonopoly(state, square);
            if (definition.housePurchaseCost * buyCount <= excess - unmortgageCost)
                return HousePurchaseDecision::Yes;
        }

        return foundDeferredMonopoly
            ? HousePurchaseDecision::Later : HousePurchaseDecision::No;
    }

    bool shouldGiveAwayMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player,
        CashStrategy strategy,
        std::int64_t minCashOnHand,
        int maxHousesPerSquareForGiveAway,
        std::span<const std::int64_t> moneyOwed) noexcept
    {
        if (state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers || strategy >= CashStrategy::Count)
            return false;
        if (!ai::playerOwnsMonopoly(state, player, false) ||
            ai::housingShortage(state, CriticalHousingLevel) ||
            trade::findNonmonopolyPlayer(state, player, moneyOwed) == rules::NobodyPlayer ||
            trade::onlyPlayerHasMonopoly(state, player))
            return false;

        auto simulated = state;
        const auto debt = player < moneyOwed.size() ? moneyOwed[player] : 0;
        while (hypotheticalUnmortgageMonopolyProperty(
            simulated, player, strategy, minCashOnHand, debt) !=
            rules::board::SquareType::Go)
        {
        }
        while (hypotheticalBuyHouse(simulated, player, debt))
        {
        }

        const auto monopolies = ai::monopoliesOwned(simulated, player, false);
        for (std::size_t index = 0; index < monopolies.count; ++index)
        {
            const auto representative = monopolies.representatives[index];
            const auto lots = ai::monopolyLots(representative);
            const auto houses = ai::housesOnMonopoly(simulated, representative);
            if (houses <= static_cast<std::int64_t>(lots.count) *
                    maxHousesPerSquareForGiveAway)
                return true;
        }
        return false;
    }

}
