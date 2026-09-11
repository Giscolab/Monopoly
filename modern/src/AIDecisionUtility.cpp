#include "AIDecisionUtility.hpp"
#include "AITradeUtility.hpp"

#include <algorithm>
#include <cmath>
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

        inline constexpr std::array<rules::board::SquareType, 28> MortgageOrder{
            rules::board::SquareType::MediterraneanAvenue,
            rules::board::SquareType::OrientalAvenue,
            rules::board::SquareType::VermontAvenue,
            rules::board::SquareType::BalticAvenue,
            rules::board::SquareType::ConnecticutAvenue,
            rules::board::SquareType::StCharlesPlace,
            rules::board::SquareType::StatesAvenue,
            rules::board::SquareType::VirginiaAvenue,
            rules::board::SquareType::StJamesPlace,
            rules::board::SquareType::TennesseeAvenue,
            rules::board::SquareType::NewYorkAvenue,
            rules::board::SquareType::KentuckyAvenue,
            rules::board::SquareType::IndianaAvenue,
            rules::board::SquareType::IllinoisAvenue,
            rules::board::SquareType::AtlanticAvenue,
            rules::board::SquareType::VentnorAvenue,
            rules::board::SquareType::MarvinGardens,
            rules::board::SquareType::PacificAvenue,
            rules::board::SquareType::NorthCarolinaAvenue,
            rules::board::SquareType::PennsylvaniaAvenue,
            rules::board::SquareType::ParkPlace,
            rules::board::SquareType::Boardwalk,
            rules::board::SquareType::ShortLineRailroad,
            rules::board::SquareType::BAndORailroad,
            rules::board::SquareType::PennsylvaniaRailroad,
            rules::board::SquareType::ReadingRailroad,
            rules::board::SquareType::ElectricCompany,
            rules::board::SquareType::WaterWorks};
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

    rules::board::SquareType hypotheticalUnmortgageProperty(
        rules::GameState& state,
        rules::PlayerNumber player,
        bool onlyMonopolies,
        CashStrategy strategy,
        std::int64_t minCashOnHand,
        std::int64_t moneyOwed) noexcept
    {
        using rules::board::SquareType;
        if (state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers || strategy >= CashStrategy::Count)
            return SquareType::Go;

        const auto monopoly = hypotheticalUnmortgageMonopolyProperty(
            state, player, strategy, minCashOnHand, moneyOwed);
        if (monopoly != SquareType::Go || onlyMonopolies)
            return monopoly;

        const auto owned = ai::propertiesOwnedByPlayer(state, player);
        const auto excessCash = excessCashAvailable(
            state, player, true, strategy, minCashOnHand, moneyOwed);
        const auto square = ai::findHighestRentMortgaged(
            state, player, owned, excessCash);
        if (square == SquareType::Go)
            return SquareType::Go;

        const auto rawCost = static_cast<double>(
            rules::board::definition(square).mortgageCost) * 1.1;
        // Retail's hypothetical non-monopoly fallback debits a stale
        // Square_predefined_info pointer. Use the selected property's cost.
        state.squares[static_cast<std::size_t>(square)].mortgaged = false;
        state.players[player].cash -= static_cast<std::int64_t>(rawCost);
        return square;
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

    std::int64_t totalWorthWithFactors(
        const rules::GameState& state,
        rules::PlayerNumber player,
        const WorthFactors& factors) noexcept
    {
        using rules::board::SquareGroup;
        using rules::board::SquareType;
        if (state.numberOfPlayers > rules::MaxPlayers || player >= state.numberOfPlayers)
            return 0;

        const auto owned = ai::propertiesOwnedByPlayer(state, player);
        const auto railroadCount = ai::numberRailroadsUtilitiesOwned(
            state, player, SquareGroup::Railroad, false, owned);
        const auto utilityCount = ai::numberRailroadsUtilitiesOwned(
            state, player, SquareGroup::Utility, false, owned);

        std::int64_t total{};
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(SquareType::InJail); ++index)
        {
            const auto square = static_cast<SquareType>(index);
            const auto bit = rules::board::propertyBit(square);
            if (bit == 0 || (owned & bit) == 0)
                continue;

            const auto& definition = rules::board::definition(square);
            total += definition.housePurchaseCost * state.squares[index].houses;

            double factor{};
            if (definition.group == SquareGroup::Railroad && railroadCount > 0)
            {
                const auto factorIndex = static_cast<std::size_t>(railroadCount - 1);
                if (factorIndex < 4)
                    factor = factors.cashCow[factorIndex];
            }
            else if (definition.group == SquareGroup::Utility && utilityCount > 0)
            {
                const auto factorIndex = static_cast<std::size_t>(4 + utilityCount - 1);
                if (factorIndex < factors.cashCow.size())
                    factor = factors.cashCow[factorIndex];
            }
            else
            {
                const auto factorIndex = static_cast<std::size_t>(definition.group);
                if (factorIndex < factors.property.size())
                    factor = factors.property[factorIndex];
            }
            total += static_cast<std::int64_t>(
                static_cast<double>(definition.purchaseCost) * factor);
        }
        return total + state.players[player].cash;
    }

    bool mortgageWorstProperty(
        rules::GameState& state,
        rules::PlayerNumber player,
        bool sellHouses,
        bool mortgageMonopoly) noexcept
    {
        using rules::board::SquareGroup;
        using rules::board::SquareType;
        if (state.numberOfPlayers > rules::MaxPlayers || player >= state.numberOfPlayers)
            return false;

        const auto owned = ai::propertiesOwnedByPlayer(state, player);
        auto monopoliless = owned;
        auto housingless = owned;

        const auto mortgage = [&](SquareType square) {
            const auto index = static_cast<std::size_t>(square);
            state.players[player].cash += rules::board::definition(square).mortgageCost;
            state.squares[index].mortgaged = true;
        };

        for (const auto square : MortgageOrder)
        {
            const auto bit = rules::board::propertyBit(square);
            if (bit == 0 || (owned & bit) == 0)
                continue;

            const auto index = static_cast<std::size_t>(square);
            // Retail tests unsigned-char houses >= 0, which is always true and
            // empties the intended "housingless" set. Keep only genuinely
            // undeveloped lots so the documented fallback can actually run.
            if (state.squares[index].houses > 0)
                housingless &= ~bit;

            const bool monopoly = ai::ownsMonopoly(
                state, player, square, mortgageMonopoly);
            if (monopoly)
                monopoliless &= ~bit;

            if (state.squares[index].mortgaged)
                continue;

            const auto group = rules::board::definition(square).group;
            int groupCount{};
            if (group == SquareGroup::Railroad || group == SquareGroup::Utility)
            {
                groupCount = ai::numberRailroadsUtilitiesOwned(
                    state, player, group,
                    state.options.mortgagedCountsInGroupRent, owned);
            }

            if (!monopoly && groupCount < 2)
            {
                mortgage(square);
                return true;
            }
        }

        SquareType mortgageSquare = SquareType::Go;
        if (!sellHouses && !mortgageMonopoly)
        {
            mortgageSquare = ai::findLowestRentProperty(
                state, player, monopoliless);
        }
        else if (!sellHouses)
        {
            mortgageSquare = ai::findLowestRentProperty(
                state, player, housingless);
        }
        else if (ai::housingShortage(state, CriticalHousingLevel))
        {
            mortgageSquare = ai::findLowestRentProperty(
                state, player, housingless);
            if (mortgageSquare == SquareType::Go)
                mortgageSquare = ai::findLowestRentProperty(state, player, owned);
        }
        else
        {
            mortgageSquare = ai::findLowestRentProperty(state, player, owned);
        }

        if (mortgageSquare == SquareType::Go)
            return false;

        if (ai::housesOnMonopoly(state, mortgageSquare) > 0)
        {
            const auto lots = ai::monopolyLots(mortgageSquare);
            ai::testSellHouses(state, lots, mortgageSquare);
        }
        else
        {
            mortgage(mortgageSquare);
        }
        return true;
    }

    void mortgageNegativeCashPlayers(rules::GameState& state) noexcept
    {
        if (state.numberOfPlayers > rules::MaxPlayers)
            return;
        for (rules::PlayerNumber player = 0; player < state.numberOfPlayers; ++player)
        {
            if (state.players[player].currentSquare ==
                static_cast<std::uint8_t>(rules::board::SquareType::OffBoard))
                continue;
            while (state.players[player].cash < 0)
            {
                if (!mortgageWorstProperty(state, player, true, true))
                    break;
            }
        }
    }

    double evaluateWinningChances(
        const rules::GameState& state,
        rules::PlayerNumber player,
        const WinningChanceConfig& config,
        std::span<double> savePlayerChances) noexcept
    {
        using rules::board::SquareType;
        if (state.numberOfPlayers == 0 || state.numberOfPlayers > rules::MaxPlayers)
            return 0.0;
        const bool saveAll = !savePlayerChances.empty();
        if ((!saveAll && player >= state.numberOfPlayers) ||
            (saveAll && savePlayerChances.size() < state.numberOfPlayers))
            return 0.0;

        std::array<double, rules::MaxPlayers> localChances{};
        double* chances = saveAll ? savePlayerChances.data() : localChances.data();
        std::fill(chances, chances + state.numberOfPlayers, 0.0);
        auto simulated = state;
        const bool shortage = ai::housingShortage(state, CriticalHousingLevel);

        for (rules::PlayerNumber current = 0; current < simulated.numberOfPlayers; ++current)
        {
            if (state.players[current].currentSquare ==
                static_cast<std::uint8_t>(SquareType::OffBoard))
                continue;
            do
            {
                if (!shortage)
                {
                    while (hypotheticalBuyHouse(
                        simulated, current, config.moneyOwed[current]))
                    {
                    }
                }
            } while (hypotheticalUnmortgageProperty(
                simulated, current, true, config.cashStrategy[current],
                config.minCashOnHand[current], config.moneyOwed[current]) != SquareType::Go);
        }

        for (rules::PlayerNumber current = 0; current < simulated.numberOfPlayers; ++current)
        {
            if (state.players[current].currentSquare ==
                static_cast<std::uint8_t>(SquareType::OffBoard))
                continue;
            while (hypotheticalUnmortgageProperty(
                simulated, current, false, config.cashStrategy[current],
                config.minCashOnHand[current], config.moneyOwed[current]) != SquareType::Go)
            {
            }
        }

        for (rules::PlayerNumber current = 0; current < simulated.numberOfPlayers; ++current)
        {
            if (state.players[current].currentSquare ==
                static_cast<std::uint8_t>(SquareType::OffBoard))
                continue;
            while (simulated.players[current].cash < 0)
            {
                if (!mortgageWorstProperty(simulated, current, false, false) &&
                    !mortgageWorstProperty(simulated, current, false, true) &&
                    !mortgageWorstProperty(simulated, current, true, true))
                    break;
            }
        }

        double totalAbsoluteChance{};
        for (rules::PlayerNumber current = 0; current < simulated.numberOfPlayers; ++current)
        {
            const auto owned = ai::propertiesOwnedByPlayer(state, current);
            chances[current] = ai::averageRentReceived(
                simulated, current, 0, false, 0.07, owned) *
                static_cast<double>(simulated.numberOfPlayers - 1);
            chances[current] -= static_cast<double>(
                ai::averageRentPaid(simulated, current, 0, true, 0.07));
            chances[current] += static_cast<double>(state.options.passingGoAmount);

            const auto liquidAssets = ai::liquidAssets(
                simulated, current, false, false, config.moneyOwed[current]);
            switch (ai::monopolyStage(simulated, current))
            {
            case ai::MonopolyStage::Buying:
            case ai::MonopolyStage::NoMonopolies:
            {
                constexpr double MonopolySquareCount = 22.0;
                const double buyingFraction =
                    static_cast<double>(ai::propertiesLeftToBuy(state)) / MonopolySquareCount;
                double multiplier =
                    config.buyingStageCashMultiplier * buyingFraction +
                    config.noMonopolyStageCashMultiplier * (1.0 - buyingFraction);
                const double dependence = config.cashLiquidAssetsDependence > 0.0
                    ? config.cashLiquidAssetsDependence : 1.0;
                double assetScale = 1.0 / std::exp(
                    std::log(dependence) * static_cast<double>(liquidAssets) / 1000.0);
                assetScale = std::clamp(assetScale, 0.25, 1.0);
                multiplier *= assetScale;
                chances[current] += multiplier * static_cast<double>(liquidAssets);
                break;
            }
            case ai::MonopolyStage::MonopoliesNotOwnOne:
                chances[current] += config.monopolyNotOwnedStageCashMultiplier *
                    static_cast<double>(liquidAssets);
                break;
            case ai::MonopolyStage::MonopoliesOwnOne:
                chances[current] += config.monopolyOwnedStageCashMultiplier *
                    static_cast<double>(liquidAssets);
                break;
            }

            if (chances[current] > 0.0)
                totalAbsoluteChance += chances[current];
        }

        if (totalAbsoluteChance == 0.0)
            totalAbsoluteChance = 1.0;

        double normalizeConstant{};
        for (rules::PlayerNumber current = 0; current < simulated.numberOfPlayers; ++current)
        {
            chances[current] /= totalAbsoluteChance;
            chances[current] = std::exp(chances[current]);
            normalizeConstant += chances[current];
        }
        for (rules::PlayerNumber current = 0; current < simulated.numberOfPlayers; ++current)
            chances[current] /= normalizeConstant;

        if (saveAll)
            return 0.0;
        return chances[player];
    }

    double evaluateTrade(
        const rules::GameState& state,
        rules::PlayerNumber player,
        rules::PlayerNumber strategyPlayer,
        const ai::trade::TradeProposalList& proposals,
        const TradeEvaluationConfig& config) noexcept
    {
        if (state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers || strategyPlayer >= state.numberOfPlayers)
            return -50.0;

        auto before = state;
        auto after = state;
        ai::trade::applyTradeToState(after, proposals);
        mortgageNegativeCashPlayers(after);
        if (config.purchasingPlayer < state.numberOfPlayers &&
            config.purchasingProperty < rules::board::SquareType::InJail)
        {
            const auto index = static_cast<std::size_t>(config.purchasingProperty);
            before.squares[index].owner = config.purchasingPlayer;
            after.squares[index].owner = config.purchasingPlayer;
        }

        const double chancesBefore = evaluateWinningChances(
            before, player, config.winningChance);
        const double chancesAfter = evaluateWinningChances(
            after, player, config.winningChance);
        if ((chancesBefore - chancesAfter) > config.chancesThreshold)
            return -50.0;

        auto worthBefore = totalWorthWithFactors(before, player, config.worthFactors);
        auto worthAfter = totalWorthWithFactors(after, player, config.worthFactors);
        const auto debt = config.winningChance.moneyOwed[player];
        if (ai::liquidAssets(after, player, true, true, debt) <= 0)
            return -50.0;
        for (std::size_t deck = 0;
             deck < static_cast<std::size_t>(rules::DeckType::Count); ++deck)
        {
            if (before.cards[deck].jailOwner == player)
                worthBefore += config.jailCardValue;
            if (after.cards[deck].jailOwner == player)
                worthAfter += config.jailCardValue;
        }

        const double propertyImportance = ai::trade::calculateTradePropertyImportance(
            before, after, player, strategyPlayer, proposals, false,
            config.propertyImportance, config.winningChance.moneyOwed);
        return (chancesAfter - chancesBefore) * config.chancesFactor +
            static_cast<double>(worthAfter - worthBefore) * config.cashFactor +
            propertyImportance * config.tradeImportanceFactor;
    }

    void evaluateTradePlayerList(
        const rules::GameState& state,
        std::span<const rules::PlayerNumber> players,
        rules::PlayerNumber strategyPlayer,
        const ai::trade::TradeProposalList& proposals,
        bool givingMonopolyForCash,
        const TradeEvaluationConfig& config,
        std::span<double> evaluations) noexcept
    {
        if (state.numberOfPlayers == 0 || state.numberOfPlayers > rules::MaxPlayers ||
            strategyPlayer >= state.numberOfPlayers || evaluations.size() < players.size())
            return;

        auto before = state;
        auto after = state;
        ai::trade::applyTradeToState(after, proposals);
        mortgageNegativeCashPlayers(after);
        if (config.purchasingPlayer < state.numberOfPlayers &&
            config.purchasingProperty < rules::board::SquareType::InJail)
        {
            const auto index = static_cast<std::size_t>(config.purchasingProperty);
            before.squares[index].owner = config.purchasingPlayer;
            after.squares[index].owner = config.purchasingPlayer;
        }
        std::array<double, rules::MaxPlayers> chancesBefore{};
        std::array<double, rules::MaxPlayers> chancesAfter{};
        (void)evaluateWinningChances(
            before, rules::NobodyPlayer, config.winningChance,
            std::span<double>(chancesBefore.data(), state.numberOfPlayers));
        (void)evaluateWinningChances(
            after, rules::NobodyPlayer, config.winningChance,
            std::span<double>(chancesAfter.data(), state.numberOfPlayers));

        for (std::size_t index = 0; index < players.size(); ++index)
        {
            const auto player = players[index];
            if (player >= state.numberOfPlayers)
            {
                evaluations[index] = -50.0;
                continue;
            }
            if ((chancesBefore[player] - chancesAfter[player]) > config.chancesThreshold)
            {
                evaluations[index] = -50.0;
                continue;
            }

            auto worthBefore = totalWorthWithFactors(before, player, config.worthFactors);
            auto worthAfter = totalWorthWithFactors(after, player, config.worthFactors);
            const auto debt = config.winningChance.moneyOwed[player];
            if (ai::liquidAssets(after, player, true, true, debt) <= 0)
            {
                evaluations[index] = -50.0;
                continue;
            }
            for (std::size_t deck = 0;
                 deck < static_cast<std::size_t>(rules::DeckType::Count); ++deck)
            {
                if (before.cards[deck].jailOwner == player)
                    worthBefore += config.jailCardValue;
                if (after.cards[deck].jailOwner == player)
                    worthAfter += config.jailCardValue;
            }

            const double propertyImportance = ai::trade::calculateTradePropertyImportance(
                before, after, player, strategyPlayer, proposals,
                givingMonopolyForCash, config.propertyImportance,
                config.winningChance.moneyOwed);
            evaluations[index] =
                (chancesAfter[player] - chancesBefore[player]) * config.chancesFactor +
                static_cast<double>(worthAfter - worthBefore) * config.cashFactor +
                propertyImportance * config.tradeImportanceFactor;
        }
    }

    bool makeTradeFair(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::span<const rules::PlayerNumber> partners,
        std::int64_t giveMost,
        bool givingMonopoly,
        ai::trade::TradeProposalList& proposals,
        const FairTradeConfig& config,
        std::span<const ai::trade::FutureImmunityRecord> immunities) noexcept
    {
        if (state.numberOfPlayers == 0 || state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers || partners.empty() ||
            partners.size() >= rules::MaxPlayers)
            return false;

        std::array<rules::PlayerNumber, rules::MaxPlayers> participants{};
        for (std::size_t index = 0; index < partners.size(); ++index)
        {
            if (partners[index] >= state.numberOfPlayers || partners[index] == player)
                return false;
            participants[index] = partners[index];
        }
        participants[partners.size()] = player;
        const auto participantCount = partners.size() + 1;
        std::array<std::int64_t, rules::MaxPlayers> cashGiven{};
        auto change = giveMost / 2;
        if (partners.size() == 1)
        {
            proposals[player].cashReceived = 0;
            proposals[player].cashGiven = 0;
            proposals[partners[0]].cashReceived = 0;
            proposals[partners[0]].cashGiven = 0;
        }

        const double minEvaluation = givingMonopoly
            ? config.minGiveMonopolyEvaluation
            : config.minEvaluationThreshold;
        bool doneChangingCash{};
        std::array<double, rules::MaxPlayers> evaluations{};
        while (!doneChangingCash)
        {
            evaluateTradePlayerList(
                state,
                std::span<const rules::PlayerNumber>(participants.data(), participantCount),
                player, proposals, givingMonopoly, config.evaluation,
                std::span<double>(evaluations.data(), participantCount));

            auto bestEvaluation = evaluations[0];
            auto worstEvaluation = evaluations[0];
            auto bestPlayer = partners[0];
            auto worstPlayer = partners[0];
            for (std::size_t index = 0; index < partners.size(); ++index)
            {
                const auto current = partners[index];
                if (evaluations[index] > bestEvaluation)
                {
                    bestEvaluation = evaluations[index];
                    bestPlayer = current;
                }
                if (evaluations[index] < worstEvaluation)
                {
                    worstEvaluation = evaluations[index];
                    worstPlayer = current;
                }
            }

            if (evaluations[partners.size()] < minEvaluation ||
                worstEvaluation >= evaluations[partners.size()])
            {
                if (change <= 2)
                {
                    change = 5;
                    doneChangingCash = true;
                }
                proposals[player].cashReceived += change;
                proposals[bestPlayer].cashGiven += change;
                cashGiven[bestPlayer] -= change;
            }
            else
            {
                if (change <= 2)
                {
                    doneChangingCash = true;
                    continue;
                }
                proposals[worstPlayer].cashReceived += change;
                proposals[player].cashGiven += change;
                cashGiven[worstPlayer] += change;
            }
            change /= 2;
        }

        for (const auto current : partners)
        {
            const double multiplier = ai::trade::cashMultiplier(
                config.playerAttitude[current], config.cashMultipliers);
            if (multiplier <= 0.0)
                return false;
            const double factor = 1.0 / multiplier;
            std::int64_t adjustment{};
            if (cashGiven[current] > 0)
                adjustment = static_cast<std::int64_t>(
                    static_cast<double>(cashGiven[current]) -
                    static_cast<double>(cashGiven[current]) * factor);
            else
                adjustment = static_cast<std::int64_t>(
                    static_cast<double>(cashGiven[current]) -
                    static_cast<double>(cashGiven[current]) / factor);
            proposals[current].cashReceived -= adjustment;
            proposals[player].cashGiven -= adjustment;
        }
        for (std::size_t index = 0; index < participantCount; ++index)
        {
            const auto current = participants[index];
            proposals[current].cashGiven -= proposals[current].cashReceived;
            proposals[current].cashReceived = 0;
            if (proposals[current].cashGiven < 0)
            {
                proposals[current].cashReceived = -proposals[current].cashGiven;
                proposals[current].cashGiven = 0;
            }

            auto canPay = -ai::trade::transferTax(
                state, proposals[current].propertiesReceived);
            auto properties = ai::propertiesOwnedByPlayer(state, current);
            properties &= ~proposals[current].propertiesGiven;
            canPay += ai::liquidAssetsForProperties(state, properties, false, false);
            canPay += state.players[current].cash;
            if (config.localAIPlayer[current])
                canPay -= config.evaluation.winningChance.moneyOwed[current];

            if (canPay < proposals[current].cashGiven)
            {
                if (!givingMonopoly || canPay < -cashGiven[current])
                    return false;
                change = (canPay - cashGiven[current]) / 2;
                change -= proposals[current].cashGiven;
                proposals[current].cashGiven += change;
                proposals[player].cashReceived += change;
                break;
            }
        }
        return ai::trade::tradeIsProper(state, proposals, immunities);
    }

    bool buildMonopolyTrade(
        const rules::GameState& state,
        rules::PlayerNumber player,
        rules::board::SquareGroup group,
        std::span<const rules::PlayerNumber> partners,
        const ai::trade::PropertySets& properties,
        ai::trade::TradeProposalList& proposals,
        const MonopolyProposalConfig& config) noexcept
    {
        const auto groupIndex = static_cast<std::size_t>(group);
        if (state.numberOfPlayers == 0 || state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers || groupIndex >= ai::ExpensiveMonopolySquares.size() ||
            partners.empty() || partners.size() >= rules::MaxPlayers)
            return false;
        auto combined = properties[player];
        for (const auto partner : partners)
        {
            if (partner >= state.numberOfPlayers || partner == player)
                return false;
            combined |= properties[partner];
        }
        auto next = proposals;
        const auto lots = ai::monopolyLots(ai::ExpensiveMonopolySquares[groupIndex]);
        for (std::size_t index = 0; index < lots.count; ++index)
        {
            const auto square = lots.squares[index];
            const auto owner = state.squares[static_cast<std::size_t>(square)].owner;
            if (owner == player)
                continue;
            if (owner >= state.numberOfPlayers)
                return false;
            const auto bit = rules::board::propertyBit(square);
            next[player].propertiesReceived |= bit;
            next[owner].propertiesGiven |= bit;
        }
        combined &= ~next[player].propertiesReceived;
        auto after = state;
        for (const auto partner : partners)
        {
            // Preserve retail's successive updates of the same simulation.
            ai::trade::applyTradeToState(after, next);
            (void)ai::trade::addTypeProperty(
                state, after, partner, player, combined,
                ai::trade::TradeImportanceItem::Monopoly, next, properties, false,
                config.whatToTrade, config.fairTrade.playerAttitude[partner],
                config.fairTrade.evaluation.winningChance.moneyOwed[partner]);
        }
        if (!makeTradeFair(state, player, partners, 2500, false, next, config.fairTrade))
            return false;
        proposals = next;
        return true;
    }

    bool buildMonopolyForCash(
        const rules::GameState& state,
        rules::PlayerNumber player,
        rules::PlayerNumber firstPartner,
        const ai::trade::PropertySets& properties,
        ai::trade::TradeProposalList& proposals,
        const MonopolyProposalConfig& config) noexcept
    {
        if (state.numberOfPlayers == 0 || state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers || firstPartner >= state.numberOfPlayers)
            return false;
        auto available = properties[player];
        const auto owned = ai::monopoliesOwned(state, player, false);
        for (std::size_t index = 0; index < owned.count; ++index)
            available ^= ai::monopolySet(owned.representatives[index]);

        for (std::size_t offset = 0; offset < state.numberOfPlayers; ++offset)
        {
            const auto partner = static_cast<rules::PlayerNumber>(
                (firstPartner + offset) % state.numberOfPlayers);
            if (partner == player ||
                state.players[partner].currentSquare ==
                    static_cast<std::uint8_t>(rules::board::SquareType::OffBoard) ||
                ai::playerOwnsMonopoly(state, partner, false))
                continue;
            ai::trade::TradeProposalList next{};
            auto combined = available | properties[partner];
            if (!ai::trade::addTypeProperty(
                    state, state, partner, player, combined,
                    ai::trade::TradeImportanceItem::Monopoly, next, properties, false,
                    config.whatToTrade, config.fairTrade.playerAttitude[partner],
                    config.fairTrade.evaluation.winningChance.moneyOwed[partner]))
                continue;
            const std::array<rules::PlayerNumber, 1> partners{partner};
            if (makeTradeFair(state, player, partners, 16000, true, next, config.fairTrade) &&
                ai::trade::tradeIsProper(state, next))
            {
                proposals = next;
                return true;
            }
        }
        return false;
    }

    CounterProposalPreflightResult counterProposalPreflight(
        const rules::GameState& state,
        rules::PlayerNumber player,
        const ai::trade::TradeProposalList& currentTrade,
        const CounterProposalPreflightInputs& inputs,
        const CounterProposalPreflightConfig& config,
        CounterProposalSession& session,
        std::span<const ai::trade::FutureImmunityRecord> immunities) noexcept
    {
        CounterProposalPreflightResult result{};
        if (state.numberOfPlayers == 0 || state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers || inputs.proposedPlayer >= state.numberOfPlayers)
            return result;

        if (inputs.pendingActions != 0 || !state.tradeInProgress ||
            inputs.playerSendingTrade || inputs.auctionOn)
        {
            result.status = CounterProposalStatus::InvalidTime;
            return result;
        }
        if (!ai::trade::playerInvolvedInTrade(currentTrade[player]))
        {
            result.status = CounterProposalStatus::NotInvolved;
            return result;
        }
        if (ai::trade::playerHasFutureOrImmunity(player, immunities))
        {
            result.status = CounterProposalStatus::FutureOrImmunity;
            return result;
        }
        if (session.timesCounteredTrade >= config.tradeCounterLimit)
        {
            result.status = CounterProposalStatus::TooManyCounters;
            return result;
        }
        if (session.timesCounteredTrade == 0 &&
            ai::trade::propertiesAlreadyTraded(
                session.propertyMemory, inputs.proposedPlayer,
                currentTrade[player], config.numberTimesAllowPropertyTrade) != 0)
        {
            result.status = CounterProposalStatus::RepeatedProperties;
            return result;
        }
        if (session.timesCounteredTrade == 0)
            ai::trade::rememberTradedProperties(
                session.propertyMemory, inputs.proposedPlayer, currentTrade[player]);

        if (inputs.counterRoll > config.tradeCounterProbability &&
            inputs.tradeAccept && session.timesCounteredTrade == 0)
        {
            result.status = CounterProposalStatus::ProbabilitySkipped;
            return result;
        }
        result.evaluation = evaluateTrade(
            state, player, player, currentTrade, config.evaluation);
        if (session.timesCounteredTrade != 0 &&
            result.evaluation <= session.lastTradeEvaluation)
        {
            result.status = CounterProposalStatus::NotSeriousTrader;
            return result;
        }
        session.lastTradeEvaluation = result.evaluation;

        auto preparedState = state;
        if (config.evaluation.purchasingPlayer < state.numberOfPlayers &&
            config.evaluation.purchasingProperty < rules::board::SquareType::InJail)
            preparedState.squares[static_cast<std::size_t>(
                config.evaluation.purchasingProperty)].owner = config.evaluation.purchasingPlayer;
        result.preparation = ai::trade::createPlayerPropertyAttitudeList(
            preparedState, player, currentTrade, config.playerAttitude);
        if (result.preparation.tradePlayerCount < 1)
        {
            result.status = CounterProposalStatus::NoTradePartners;
            return result;
        }
        if (result.preparation.tradePlayerCount == 1)
        {
            const auto partner = result.preparation.tradePlayers[0];
            if (config.playerAttitude[partner] <= -1.0)
            {
                result.status = CounterProposalStatus::AnnoyedWithTrader;
                return result;
            }
        }
        result.averageAttitude = result.preparation.totalAttitude /
            static_cast<double>(result.preparation.tradePlayerCount);
        if (result.averageAttitude < -1.0)
            result.averageAttitude = -1.0;
        result.status = CounterProposalStatus::Ready;
        return result;
    }

    CounterProposalBalanceResult counterProposalBalance(
        const rules::GameState& state,
        rules::PlayerNumber player,
        const CounterProposalPreflightResult& preflight,
        ai::trade::TradeProposalList& proposals,
        const CounterProposalBalanceConfig& config,
        std::span<const ai::trade::FutureImmunityRecord> immunities) noexcept
    {
        CounterProposalBalanceResult result{};
        if (state.numberOfPlayers == 0 || state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers ||
            preflight.status != CounterProposalStatus::Ready ||
            preflight.preparation.tradePlayerCount == 0 ||
            preflight.preparation.tradePlayerCount >= rules::MaxPlayers ||
            config.maxIterations == 0)
            return result;

        const auto partnerCount = preflight.preparation.tradePlayerCount;
        for (std::size_t index = 0; index < partnerCount; ++index)
        {
            const auto partner = preflight.preparation.tradePlayers[index];
            if (partner >= state.numberOfPlayers || partner == player)
                return result;
        }

        auto oldState = state;
        auto newState = state;
        ai::trade::applyTradeToState(newState, proposals);
        const auto& evaluationConfig = config.fairTrade.evaluation;
        if (evaluationConfig.purchasingPlayer < state.numberOfPlayers &&
            evaluationConfig.purchasingProperty < rules::board::SquareType::InJail)
        {
            const auto square = static_cast<std::size_t>(evaluationConfig.purchasingProperty);
            oldState.squares[square].owner = evaluationConfig.purchasingPlayer;
            newState.squares[square].owner = evaluationConfig.purchasingPlayer;
        }
        auto combinedProperties = rules::board::PropertySet{};
        auto combinedTraded = rules::board::PropertySet{};
        for (std::size_t index = 0; index < partnerCount; ++index)
        {
            const auto partner = preflight.preparation.tradePlayers[index];
            combinedProperties |= preflight.preparation.playerProperties[partner];
            combinedTraded |= proposals[partner].propertiesReceived |
                proposals[partner].propertiesGiven;
        }
        combinedProperties |= preflight.preparation.playerProperties[player];
        combinedProperties &= ~combinedTraded;

        result.propertyImportance = 0.0;
        do
        {
            result.evaluation = evaluateTrade(
                oldState, player, player, proposals, evaluationConfig);
            if (result.evaluation >= config.minEvaluationThreshold)
                break;

            // Retail repeatedly applies the full trade to new_state here. This
            // intentionally reapplies cash deltas while property ownership stays idempotent.
            ai::trade::applyTradeToState(newState, proposals);
            result.propertyImportance = ai::trade::calculateTradePropertyImportance(
                oldState, newState, player, player, proposals, false,
                evaluationConfig.propertyImportance,
                evaluationConfig.winningChance.moneyOwed);
            const auto importance = ai::trade::createItemImportanceList(
                oldState, player, player, evaluationConfig.propertyImportance,
                0.0, config.fairTrade.cashMultipliers);
            bool added{};
            for (const auto& record : importance)
            {
                if (-result.propertyImportance < record.importance)
                    continue;
                if (ai::trade::addTypeProperty(
                        state, newState, player, player, combinedProperties,
                        record.item, proposals, preflight.preparation.playerProperties,
                        false, config.whatToTrade, 0.0,
                        evaluationConfig.winningChance.moneyOwed[player]))
                {
                    added = true;
                    break;
                }
            }

            ++result.iterations;
            if (!added || result.iterations >= config.maxIterations)
                break;
        }
        while (result.propertyImportance < 0.0);

        if (result.propertyImportance < config.lowestPropertyImportanceForCounter &&
            result.evaluation < config.minEvaluationThreshold)
        {
            result.status = CounterProposalBalanceStatus::TooPoor;
            return result;
        }

        double gaveMonopolyValue{};
        if (partnerCount == 1)
        {
            const auto target = preflight.preparation.tradePlayers[0];
            const auto importance = ai::trade::createItemImportanceList(
                oldState, target, player, evaluationConfig.propertyImportance,
                config.fairTrade.playerAttitude[target],
                config.fairTrade.cashMultipliers);
            for (const auto& record : importance)
            {
                ai::trade::applyTradeToState(newState, proposals);
                result.propertyImportance = ai::trade::calculateTradePropertyImportance(
                    oldState, newState, target, player, proposals, false,
                    evaluationConfig.propertyImportance,
                    evaluationConfig.winningChance.moneyOwed);
                result.propertyImportance += gaveMonopolyValue;
                if (-result.propertyImportance < record.importance)
                    continue;

                if (record.item == ai::trade::TradeImportanceItem::Monopoly)
                {
                    proposals[player].propertiesGiven = 0;
                    proposals[target].propertiesReceived = 0;
                    newState = state;
                    ai::trade::applyTradeToState(newState, proposals);
                    result.propertyImportance = ai::trade::calculateTradePropertyImportance(
                        oldState, newState, target, player, proposals, false,
                        evaluationConfig.propertyImportance,
                        evaluationConfig.winningChance.moneyOwed);
                }

                const bool added = ai::trade::addTypeProperty(
                    state, newState, target, player, combinedProperties,
                    record.item, proposals, preflight.preparation.playerProperties,
                    true, config.whatToTrade, config.fairTrade.playerAttitude[target],
                    evaluationConfig.winningChance.moneyOwed[target]);
                if (!added && record.item == ai::trade::TradeImportanceItem::Monopoly)
                {
                    result.status = CounterProposalBalanceStatus::CouldNotReturnMonopoly;
                    return result;
                }
                if (added && record.item == ai::trade::TradeImportanceItem::Monopoly)
                {
                    ai::trade::applyTradeToState(newState, proposals);
                    gaveMonopolyValue = ai::trade::calculateTradePropertyImportance(
                        oldState, newState, target, player, proposals, false,
                        evaluationConfig.propertyImportance,
                        evaluationConfig.winningChance.moneyOwed);
                    gaveMonopolyValue -= result.propertyImportance;
                    gaveMonopolyValue = record.importance - gaveMonopolyValue;
                    if (gaveMonopolyValue < 0.0)
                        gaveMonopolyValue = 0.0;
                }
            }
        }

        ai::trade::makeTradeProper(state, proposals, immunities);
        const auto partners = std::span<const rules::PlayerNumber>(
            preflight.preparation.tradePlayers.data(), partnerCount);
        if (!makeTradeFair(oldState, player, partners, config.maxGiveInTrade,
                false, proposals, config.fairTrade, immunities))
        {
            result.status = CounterProposalBalanceStatus::Unaffordable;
            return result;
        }
        if (!ai::trade::tradeIsProper(state, proposals, immunities))
        {
            result.status = CounterProposalBalanceStatus::Improper;
            return result;
        }
        result.status = CounterProposalBalanceStatus::Ready;
        return result;
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
