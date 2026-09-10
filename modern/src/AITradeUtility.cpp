#include "AITradeUtility.hpp"

#include <algorithm>
#include <cmath>

namespace monopoly::ai::trade
{
    namespace
    {
        inline constexpr std::array<std::array<double, 21>, 8> DarzinskisImportance{{
            {{-13.4, -13.5, -13.7, -14.0, -14.5, -14.9, -15.4, -16.1, -16.9, -17.8,
              -19.0, -20.0, -21.0, -22.0, -23.0, -24.0, -25.0, -26.0, -27.0, -28.0, -29.0}},
            {{10.0, 8.0, 6.0, 4.0, 2.0, 0.6, 0.0, -0.6, -1.0, -1.5,
              -2.0, -3.0, -4.0, -5.0, -6.0, -7.0, -8.0, -9.0, -10.0, -11.0, -12.0}},
            {{0.0, 0.05, 0.1, 0.15, 0.3, 0.9, 1.5, 0.5, 0.1, -0.5,
              -1.0, -1.5, -2.0, -2.8, -3.6, -4.4, -5.2, -6.0, -6.8, -7.6, -8.4}},
            {{-1.0, -0.5, -0.2, 0.1, 0.4, 1.1, 1.7, 1.3, 0.9, 0.8,
              0.7, 0.665, 0.63, 0.595, 0.560, 0.525, 0.490, 0.455, 0.42, 0.385, 0.35}},
            {{-3.0, -2.5, -2.0, -1.5, -1.0, -0.5, -0.1, 0.3, 0.5, 0.6,
              0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.4, 0.4, 0.4, 0.5}},
            {{-3.3, -2.8, -2.3, -1.8, -1.3, -0.8, -0.3, 0.1, 0.3, 0.35,
              0.39, 0.4, 0.4, 0.4, 0.4, 0.4, 0.4, 0.35, 0.35, 0.38, 0.45}},
            {{-5.0, -4.5, -4.0, -3.5, -3.0, -2.5, -2.0, -1.5, -1.0, -0.5,
              0.0, 0.15, 0.22, 0.29, 0.4, 0.45, 0.52, 0.6, 0.70, 0.78, 0.85}},
            {{-2.5, -2.0, -1.5, -1.0, -0.5, 0.0, 0.4, 0.48, 0.50, 0.49,
              0.49, 0.49, 0.49, 0.49, 0.49, 0.49, 0.48, 0.39, 0.39, 0.37, 0.36}}
        }};
        [[nodiscard]] std::size_t darzinskisIndex(std::int64_t assets) noexcept
        {
            if (assets <= 0)
                return 0;
            return static_cast<std::size_t>(std::min<std::int64_t>(assets, 5000) / 250);
        }

        [[nodiscard]] double monopolyAssetFactor(
            std::size_t group, std::size_t index, double offset, std::uint8_t aiLevel) noexcept
        {
            if (aiLevel != 3 || group >= DarzinskisImportance.size())
                return 1.0;
            const double power = std::exp(DarzinskisImportance[group][index] * std::log(2.0));
            return (power + offset) / (power + offset + 1.0);
        }

        [[nodiscard]] std::array<std::size_t, 8> monopolyImportanceOrder(
            std::int64_t assets, bool descending = true)
        {
            std::array<std::size_t, 8> order{};
            for (std::size_t index = 0; index < order.size(); ++index)
                order[index] = index;
            const auto chart = darzinskisIndex(assets);
            std::stable_sort(order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) {
                return descending
                    ? DarzinskisImportance[lhs][chart] > DarzinskisImportance[rhs][chart]
                    : DarzinskisImportance[lhs][chart] < DarzinskisImportance[rhs][chart];
            });
            return order;
        }

        [[nodiscard]] PropertyImportanceResult findPlayerPropertyImportanceAtChart(
            const rules::GameState& state,
            rules::PlayerNumber player,
            rules::PlayerNumber strategyPlayer,
            const PropertyImportanceConfig& config,
            std::size_t chart,
            std::int64_t moneyOwed) noexcept
        {
            PropertyImportanceResult result{};
            const auto liquid = ai::liquidAssets(state, player, false, false, moneyOwed);
            const auto aiLevel = state.players[strategyPlayer].aiPlayerLevel;
            for (std::size_t group = 0; group < ai::ExpensiveMonopolySquares.size(); ++group)
            {
                const auto square = ai::ExpensiveMonopolySquares[group];
                if (ai::isMonopoly(state, square) &&
                    state.squares[static_cast<std::size_t>(square)].owner == player)
                {
                    ++result.monopolies;
                    const double change = group == 0
                        ? config.propertyAllowTradeImportance
                        : config.monopolyReceivedImportance * monopolyAssetFactor(group, chart, 3.0, aiLevel);
                    result.importance += change;
                    result.monopolyImportance += change;
                }

                const int unowned = possibleMonopoly(state, player, square);
                if (unowned == -1)
                    continue;
                const double factor = monopolyAssetFactor(group, chart, 2.0, aiLevel);
                if (unowned == 1)
                {
                    const bool twoLotEdge = square == rules::board::SquareType::MediterraneanAvenue ||
                        square == rules::board::SquareType::BalticAvenue ||
                        square == rules::board::SquareType::ParkPlace ||
                        square == rules::board::SquareType::Boardwalk;
                    result.importance += (twoLotEdge ? config.propertyTwoUnownedImportance
                                                    : config.propertyOneUnownedImportance) * factor;
                }
                else if (unowned == 2)
                    result.importance += config.propertyTwoUnownedImportance * factor;
            }

            PropertySets properties{};
            std::array<rules::PlayerNumber, rules::MaxPlayers> candidates{};
            std::size_t candidateCount{};
            for (rules::PlayerNumber current = 0; current < state.numberOfPlayers; ++current)
            {
                properties[current] = ai::propertiesOwnedByPlayer(state, current);
                if (current == player || ai::playerOwnsMonopoly(state, current, false) ||
                    state.players[current].currentSquare ==
                        static_cast<std::uint8_t>(rules::board::SquareType::OffBoard))
                    continue;
                candidates[candidateCount++] = current;
            }
            const int railroads = ai::numberRailroadsUtilitiesOwned(
                state, player, rules::board::SquareGroup::Railroad, false, properties[player]);
            if (railroads > 0 && static_cast<std::size_t>(railroads) <= config.railroadImportance.size())
                result.importance += config.railroadImportance[static_cast<std::size_t>(railroads - 1)];
            const int utilities = ai::numberRailroadsUtilitiesOwned(
                state, player, rules::board::SquareGroup::Utility, false, properties[player]);
            if (utilities > 0 && static_cast<std::size_t>(utilities) <= config.utilityImportance.size())
                result.importance += config.utilityImportance[static_cast<std::size_t>(utilities - 1)];

            const auto order = monopolyImportanceOrder(liquid);
            bool firstTradeProperty = true;
            for (std::size_t rank = 0; rank + 1 < order.size(); ++rank)
            {
                const auto group = order[rank];
                const auto trade = findSmallestMonopolyTrade(
                    player, ai::monopolySet(ai::ExpensiveMonopolySquares[group]),
                    std::span<const rules::PlayerNumber>(candidates.data(), candidateCount), properties);
                if (trade.count == 0)
                    continue;
                const double factor = monopolyAssetFactor(group, chart, 1.0, aiLevel);
                result.importance += (firstTradeProperty ? config.propertyAllowTradeImportance
                                                        : config.propertyAllowMoreTradeImportance) * factor;
                firstTradeProperty = false;
            }
            const int vetoes = vetoMonopolies(properties[player]);
            if (vetoes >= 0 && static_cast<std::size_t>(vetoes) < config.monopolyVetoImportance.size())
                result.importance += config.monopolyVetoImportance[static_cast<std::size_t>(vetoes)];
            return result;
        }

        [[nodiscard]] MonopolyTradeGroup findRecursiveFromBase(
            rules::board::PropertySet baseProperties,
            rules::board::PropertySet monopoly,
            std::array<rules::PlayerNumber, rules::MaxPlayers> list,
            std::size_t count,
            const PropertySets& properties) noexcept
        {
            if (count == 0)
                return {};

            auto combined = baseProperties;
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
                const auto current = findRecursiveFromBase(
                    baseProperties, monopoly, list, count - 1, properties);
                if (current.count != 0 &&
                    (best.count == 0 || current.count < best.count))
                {
                    best = current;
                }
                std::swap(list[index], list[count - 1]);
            }

            return best;
        }

        [[nodiscard]] MonopolyTradeGroup findRecursive(
            rules::PlayerNumber player,
            rules::board::PropertySet monopoly,
            std::array<rules::PlayerNumber, rules::MaxPlayers> list,
            std::size_t count,
            const PropertySets& properties) noexcept
        {
            return findRecursiveFromBase(
                properties[player], monopoly, list, count, properties);
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

    PropertyImportanceResult findPlayerPropertyImportance(
        const rules::GameState& state,
        rules::PlayerNumber player,
        rules::PlayerNumber strategyPlayer,
        const PropertyImportanceConfig& config,
        std::int64_t moneyOwed) noexcept
    {
        if (state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers || strategyPlayer >= state.numberOfPlayers)
            return {};
        return findPlayerPropertyImportanceAtChart(
            state, player, strategyPlayer, config,
            darzinskisIndex(ai::liquidAssets(state, player, false, false, moneyOwed)), moneyOwed);
    }

    double calculateTradePropertyImportance(
        const rules::GameState& before,
        const rules::GameState& after,
        rules::PlayerNumber player,
        rules::PlayerNumber strategyPlayer,
        const TradeProposalList& proposals,
        bool givingMonopolyAway,
        const PropertyImportanceConfig& config,
        std::span<const std::int64_t> moneyOwed) noexcept
    {
        if (before.numberOfPlayers == 0 || before.numberOfPlayers > rules::MaxPlayers ||
            after.numberOfPlayers != before.numberOfPlayers || player >= before.numberOfPlayers ||
            strategyPlayer >= before.numberOfPlayers)
            return 0.0;
        const auto debtFor = [&](rules::PlayerNumber current) {
            return current < moneyOwed.size() ? moneyOwed[current] : 0;
        };
        std::array<std::size_t, rules::MaxPlayers> charts{};
        for (rules::PlayerNumber current = 0; current < before.numberOfPlayers; ++current)
            charts[current] = darzinskisIndex(ai::liquidAssets(
                after, current, false, false, debtFor(current)));

        auto filtered = proposals;
        const auto keep = ~proposals[player].propertiesGiven;
        filtered[player].propertiesReceived = 0;
        for (rules::PlayerNumber current = 0; current < before.numberOfPlayers; ++current)
            filtered[current].propertiesReceived &= keep;
        auto withoutOurProperties = before;
        applyTradeToState(withoutOurProperties, filtered);

        const auto playerBefore = findPlayerPropertyImportanceAtChart(
            before, player, strategyPlayer, config, charts[player], debtFor(player));
        const auto playerAfter = findPlayerPropertyImportanceAtChart(
            after, player, strategyPlayer, config, charts[player], debtFor(player));
        double importance = playerAfter.importance - playerBefore.importance;
        const int playerMonopolyChange = playerAfter.monopolies - playerBefore.monopolies;
        double totalMonopolyImportance{};
        int involvedOthers{};

        for (rules::PlayerNumber current = 0; current < before.numberOfPlayers; ++current)
        {
            if (current == player || !playerInvolvedInTrade(proposals[current]))
                continue;
            ++involvedOthers;
            const auto otherBefore = findPlayerPropertyImportanceAtChart(
                withoutOurProperties, current, strategyPlayer, config,
                charts[current], debtFor(current));
            const auto otherAfter = findPlayerPropertyImportanceAtChart(
                after, current, strategyPlayer, config, charts[current], debtFor(current));
            const int originalMonopolies = static_cast<int>(
                ai::monopoliesOwned(before, current, false).count);
            const int monopolyChange = otherAfter.monopolies - originalMonopolies;
            double monopolyImportanceChange =
                otherAfter.monopolyImportance - otherBefore.monopolyImportance;
            double importanceChange = otherAfter.importance - otherBefore.importance -
                monopolyImportanceChange;
            if (importanceChange < 0.0)
                importanceChange *= config.negativePropertyImportanceChangeMultiplier;
            importance -= importanceChange;
            if (monopolyChange > playerMonopolyChange)
            {
                const double weight = givingMonopolyAway
                    ? config.givingMonopolyImportance
                    : config.monopolyReceivedImportance;
                const double change = static_cast<double>(monopolyChange - playerMonopolyChange) *
                    weight * 0.875;
                importance -= change;
                if (givingMonopolyAway)
                    monopolyImportanceChange = 0.0;
                else
                    monopolyImportanceChange -= change;
            }
            totalMonopolyImportance += monopolyImportanceChange;
        }

        // Retail assumes at least one other participant. Keep the intended
        // arithmetic while avoiding a divide-by-zero for malformed proposals.
        if (involvedOthers > 0)
            importance -= totalMonopolyImportance / static_cast<double>(involvedOthers);
        return importance;
    }

    MonopolyTradeDecision shouldTradeForMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player) noexcept
    {
        if (state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers)
            return MonopolyTradeDecision::Avoid;

        if (ai::anyMonopoly(state))
        {
            return ai::playerOwnsMonopoly(state, player, false)
                ? MonopolyTradeDecision::Avoid
                : MonopolyTradeDecision::Required;
        }

        if (ai::monopolyStage(state, player) == ai::MonopolyStage::Buying)
            return MonopolyTradeDecision::Avoid;

        std::array<double, rules::MaxPlayers> averageIncome{};
        double largestIncome{};
        auto largestPlayer = rules::NobodyPlayer;
        for (rules::PlayerNumber current = 0;
             current < state.numberOfPlayers; ++current)
        {
            const auto owned = ai::propertiesOwnedByPlayer(state, current);
            averageIncome[current] = ai::averageRentReceived(
                state, current, 0, false, 1.0, owned) *
                static_cast<double>(state.numberOfPlayers - 1);
            averageIncome[current] -= static_cast<double>(
                ai::averageRentPaid(state, current, 0, true, 1.0));
            averageIncome[current] +=
                static_cast<double>(state.options.passingGoAmount);

            if (averageIncome[current] >= largestIncome)
            {
                largestIncome = averageIncome[current];
                largestPlayer = current;
            }
        }

        if (largestPlayer != rules::NobodyPlayer && largestPlayer != player)
        {
            const auto playerIncome = averageIncome[player];
            const auto gap = largestIncome - playerIncome;
            if ((playerIncome == 0.0 && gap > 0.0) ||
                (playerIncome != 0.0 && gap / playerIncome > 0.2))
                return MonopolyTradeDecision::Required;
        }

        return MonopolyTradeDecision::Maybe;
    }

    rules::PlayerNumber findNonmonopolyPlayer(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::span<const std::int64_t> moneyOwed) noexcept
    {
        if (state.numberOfPlayers > rules::MaxPlayers || player >= state.numberOfPlayers)
            return rules::NobodyPlayer;

        std::int64_t highestAssets{};
        auto highestPlayer = rules::NobodyPlayer;
        for (rules::PlayerNumber current = 0; current < state.numberOfPlayers; ++current)
        {
            if (current == player ||
                state.players[current].currentSquare ==
                    static_cast<std::uint8_t>(rules::board::SquareType::OffBoard) ||
                ai::playerOwnsMonopoly(state, current, false))
                continue;

            const auto debt = current < moneyOwed.size() ? moneyOwed[current] : 0;
            const auto assets = ai::liquidAssets(
                state, current, false, false, debt);
            if (assets > highestAssets)
            {
                highestAssets = assets;
                highestPlayer = current;
            }
        }
        return highestPlayer;
    }

    bool onlyPlayerHasMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player) noexcept
    {
        if (state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers)
            return false;

        bool playerIsOnlyOwner{};
        for (rules::PlayerNumber current = 0;
             current < state.numberOfPlayers; ++current)
        {
            if (ai::playerOwnsMonopoly(state, current, false))
            {
                if (current == player)
                    playerIsOnlyOwner = true;
                else
                    return false;
            }
            else if (current == player)
            {
                return false;
            }
        }

        if (!playerIsOnlyOwner)
            return false;

        PropertySets properties{};
        std::array<rules::PlayerNumber, rules::MaxPlayers> candidates{};
        std::size_t candidateCount{};
        for (rules::PlayerNumber current = 0;
             current < state.numberOfPlayers; ++current)
        {
            properties[current] = ai::propertiesOwnedByPlayer(state, current);
            if (current == player ||
                state.players[current].currentSquare ==
                    static_cast<std::uint8_t>(rules::board::SquareType::OffBoard))
                continue;
            candidates[candidateCount++] = current;
        }

        // Retail calls AI_Find_Smallest_Monopoly_Trade with RULE_MAX_PLAYERS
        // as a synthetic player whose property set is empty. Preserve that quirk:
        // N real opponents therefore need at least N+1 monopolies between them.
        for (const auto representative : ai::ExpensiveMonopolySquares)
        {
            const auto possible = findRecursiveFromBase(
                0, ai::monopolySet(representative), candidates, candidateCount, properties);
            if (possible.count != 0)
                return false;
        }
        return true;
    }

    int findFreeTradeSpot(
        std::span<const std::int64_t> timeLastTrade,
        std::size_t maxTrades) noexcept
    {
        if (maxTrades == 0 || maxTrades > timeLastTrade.size())
            return -1;
        for (std::size_t index = 0; index < maxTrades; ++index)
        {
            if (timeLastTrade[index] == 0)
                return static_cast<int>(index);
        }
        return -1;
    }

    bool shouldTrade(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::uint8_t importance,
        std::span<const std::int64_t> timeLastTrade,
        std::size_t maxTrades,
        const TradeCadenceInputs& inputs) noexcept
    {
        if (state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers)
            return false;

        if (inputs.playerSendingTrade || state.tradeInProgress ||
            (inputs.buySellMortgagePlayer != player &&
             inputs.buySellMortgagePlayer != rules::NobodyPlayer))
            return false;

        if (findFreeTradeSpot(timeLastTrade, maxTrades) < 0)
            return false;

        if ((importance & TradeSomewhatImportant) != 0)
            return true;

        if (inputs.shouldGiveAwayMonopoly &&
            inputs.giveAwayRoll <= inputs.giveAwayProbability)
            return true;

        const auto monopolyDecision = shouldTradeForMonopoly(state, player);
        const auto probability = monopolyDecision == MonopolyTradeDecision::Required
            ? inputs.monopolyProbability
            : inputs.proposalProbability;
        return inputs.proposalRoll <= probability;
    }

    bool addTradeMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber toPlayer,
        rules::board::PropertySet& properties,
        PropertyClassification type,
        TradeProposalList& proposals,
        std::int64_t moneyOwed) noexcept
    {
        if (state.numberOfPlayers == 0 || state.numberOfPlayers > rules::MaxPlayers ||
            toPlayer >= state.numberOfPlayers ||
            (type != PropertyClassification::Best && type != PropertyClassification::Worst))
            return false;

        const auto assets = ai::liquidAssets(state, toPlayer, false, false, moneyOwed);
        const auto order = monopolyImportanceOrder(
            assets, type == PropertyClassification::Best);
        for (const auto group : order)
        {
            const auto representative = ai::ExpensiveMonopolySquares[group];
            const auto groupSet = ai::monopolySet(representative);
            if ((properties & groupSet) != groupSet)
                continue;

            auto next = proposals;
            for (rules::PlayerNumber owner = 0; owner < state.numberOfPlayers; ++owner)
            {
                const auto owned = ai::propertiesOwnedByPlayer(state, owner) & groupSet;
                if (owner == toPlayer || owned == 0)
                    continue;
                next[toPlayer].propertiesReceived |= owned;
                next[owner].propertiesGiven |= owned;
            }
            proposals = next;
            properties &= ~groupSet;
            return true;
        }
        return false;
    }

    PlayerPropertyAttitudeList createPlayerPropertyAttitudeList(
        const rules::GameState& state,
        rules::PlayerNumber player,
        const TradeProposalList& proposals,
        std::span<const double> playerAttitudes) noexcept
    {
        PlayerPropertyAttitudeList result{};
        if (state.numberOfPlayers == 0 || state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers || playerAttitudes.size() < state.numberOfPlayers)
            return result;

        for (rules::PlayerNumber target = 0; target < state.numberOfPlayers; ++target)
        {
            const double attitude = playerAttitudes[target];
            int index = attitude <= -1.0 ? 0 : static_cast<int>((attitude + 1.0) * 10.0);
            if (index >= static_cast<int>(WhatToTradeEntries))
                index = static_cast<int>(WhatToTradeEntries) - 1;
            result.playerIndex[target] = index;
            result.playerProperties[target] = ai::propertiesOwnedByPlayer(state, target);
            if (target == player || !playerInvolvedInTrade(proposals[target]))
                continue;
            result.tradePlayers[result.tradePlayerCount++] = target;
            result.totalAttitude += attitude;
        }
        return result;
    }

    TradeImportanceList createItemImportanceList(
        const rules::GameState& state,
        rules::PlayerNumber player,
        rules::PlayerNumber strategyPlayer,
        const PropertyImportanceConfig& config,
        double strategyAttitudeTowardPlayer,
        const CashMultiplierTable& multipliers) noexcept
    {
        TradeImportanceList result{};
        if (state.numberOfPlayers == 0 || state.numberOfPlayers > rules::MaxPlayers ||
            player >= state.numberOfPlayers || strategyPlayer >= state.numberOfPlayers)
            return result;
        const double factor = player == strategyPlayer
            ? 1.0 : cashMultiplier(strategyAttitudeTowardPlayer, multipliers);
        const auto owned = ai::propertiesOwnedByPlayer(state, player);
        int rails = ai::numberRailroadsUtilitiesOwned(
            state, player, rules::board::SquareGroup::Railroad, false, owned);
        int utils = ai::numberRailroadsUtilitiesOwned(
            state, player, rules::board::SquareGroup::Utility, false, owned);
        if (rails == 4) --rails;
        if (utils == 2) --utils;

        result = {{
            {TradeImportanceItem::Monopoly, config.monopolyReceivedImportance * 0.75 * factor},
            {TradeImportanceItem::Trade, config.propertyAllowTradeImportance * factor},
            {TradeImportanceItem::TwoUnowned, config.propertyTwoUnownedImportance * factor},
            {TradeImportanceItem::OneUnowned, config.propertyOneUnownedImportance * factor},
            {TradeImportanceItem::Railroad,
                (config.railroadImportance[rails] -
                 (rails ? config.railroadImportance[rails - 1] : 0.0)) * factor},
            {TradeImportanceItem::Utility,
                (config.utilityImportance[utils] -
                 (utils ? config.railroadImportance[utils - 1] : 0.0)) * factor}
        }};

        if (strategyPlayer != player)
        {
            const auto strategyOwned = ai::propertiesOwnedByPlayer(state, strategyPlayer);
            rails = ai::numberRailroadsUtilitiesOwned(
                state, strategyPlayer, rules::board::SquareGroup::Railroad, false, strategyOwned);
            utils = ai::numberRailroadsUtilitiesOwned(
                state, strategyPlayer, rules::board::SquareGroup::Utility, false, strategyOwned);
            if (rails)
            {
                --rails;
                result[4].importance += (config.railroadImportance[rails] -
                    (rails ? config.railroadImportance[rails - 1] : 0.0)) * factor;
            }
            if (utils)
            {
                --utils;
                result[5].importance += (config.utilityImportance[utils] -
                    (utils ? config.railroadImportance[utils - 1] : 0.0)) * factor;
            }
        }

        for (std::size_t head = 0; head < result.size(); ++head)
        {
            for (std::size_t tail = head + 1; tail < result.size(); ++tail)
            {
                if (result[head].importance < result[tail].importance)
                    std::swap(result[head], result[tail]);
            }
        }
        return result;
    }

    bool addTypeProperty(
        const rules::GameState& before,
        const rules::GameState& after,
        rules::PlayerNumber player,
        rules::PlayerNumber strategyPlayer,
        rules::board::PropertySet& combinedProperties,
        TradeImportanceItem item,
        TradeProposalList& proposals,
        const PropertySets& properties,
        bool giveDescending,
        const WhatToTradeTable& whatToTrade,
        double strategyAttitudeTowardPlayer,
        std::int64_t playerMoneyOwed) noexcept
    {
        if (before.numberOfPlayers == 0 || before.numberOfPlayers > rules::MaxPlayers ||
            after.numberOfPlayers != before.numberOfPlayers ||
            player >= before.numberOfPlayers || strategyPlayer >= before.numberOfPlayers)
            return false;

        rules::board::PropertySet combinedTraded{};
        for (rules::PlayerNumber current = 0; current < before.numberOfPlayers; ++current)
            combinedTraded |= proposals[current].propertiesGiven |
                proposals[current].propertiesReceived;

        std::uint16_t monopoliesTraded{};
        for (std::size_t group = 0; group < ai::ExpensiveMonopolySquares.size(); ++group)
        {
            if ((combinedTraded & ai::monopolySet(ai::ExpensiveMonopolySquares[group])) != 0)
                monopoliesTraded |= static_cast<std::uint16_t>(1u << group);
        }

        std::array<rules::PlayerNumber, rules::MaxPlayers> candidates{};
        std::size_t candidateCount{};
        for (rules::PlayerNumber current = 0; current < after.numberOfPlayers; ++current)
        {
            if (current == player ||
                after.players[current].currentSquare ==
                    static_cast<std::uint8_t>(rules::board::SquareType::OffBoard) ||
                ai::playerOwnsMonopoly(after, current, false))
                continue;
            candidates[candidateCount++] = current;
        }

        const auto order = monopolyImportanceOrder(
            ai::liquidAssets(after, player, false, false, playerMoneyOwed),
            giveDescending);
        rules::board::PropertySet selected{};

        if (item == TradeImportanceItem::Monopoly)
        {
            if (player == strategyPlayer)
                return addTradeMonopoly(before, player, combinedProperties,
                    PropertyClassification::Best, proposals, playerMoneyOwed);

            int generosityIndex = strategyAttitudeTowardPlayer < -1.0
                ? 0 : static_cast<int>((strategyAttitudeTowardPlayer + 1.0) * 10.0);
            generosityIndex = std::clamp(generosityIndex, 0,
                static_cast<int>(WhatToTradeEntries) - 1);
            const auto& generosity = whatToTrade[static_cast<std::size_t>(generosityIndex)];
            if (generosity.giveMonopoly == PropertyClassification::Best ||
                generosity.giveMonopoly == PropertyClassification::Worst)
            {
                if (!addTradeMonopoly(before, player, combinedProperties,
                        generosity.giveMonopoly, proposals, playerMoneyOwed))
                    return false;
            }

            int counter = generosity.giveGroupTrades;
            for (; counter > 0; --counter)
            {
                if (!addTypeProperty(before, after, player, strategyPlayer,
                        combinedProperties, TradeImportanceItem::Trade, proposals,
                        properties, giveDescending, whatToTrade,
                        strategyAttitudeTowardPlayer, playerMoneyOwed) &&
                    !addTypeProperty(before, after, player, strategyPlayer,
                        combinedProperties, TradeImportanceItem::OneUnowned, proposals,
                        properties, giveDescending, whatToTrade,
                        strategyAttitudeTowardPlayer, playerMoneyOwed) &&
                    !addTypeProperty(before, after, player, strategyPlayer,
                        combinedProperties, TradeImportanceItem::TwoUnowned, proposals,
                        properties, giveDescending, whatToTrade,
                        strategyAttitudeTowardPlayer, playerMoneyOwed))
                    break;
            }
            counter += generosity.giveCashCows;
            for (; counter > 0; --counter)
            {
                if (!addTypeProperty(before, after, player, strategyPlayer,
                        combinedProperties, TradeImportanceItem::Railroad, proposals,
                        properties, giveDescending, whatToTrade,
                        strategyAttitudeTowardPlayer, playerMoneyOwed) &&
                    !addTypeProperty(before, after, player, strategyPlayer,
                        combinedProperties, TradeImportanceItem::Utility, proposals,
                        properties, giveDescending, whatToTrade,
                        strategyAttitudeTowardPlayer, playerMoneyOwed))
                    break;
            }
            counter += generosity.giveJunk;
            for (; counter > 0; --counter)
            {
                if (!addTypeProperty(before, after, player, strategyPlayer,
                        combinedProperties, TradeImportanceItem::Junk, proposals,
                        properties, giveDescending, whatToTrade,
                        strategyAttitudeTowardPlayer, playerMoneyOwed))
                    break;
            }
            return true;
        }

        if (item == TradeImportanceItem::Railroad ||
            item == TradeImportanceItem::Utility)
        {
            const auto representative = item == TradeImportanceItem::Railroad
                ? rules::board::SquareType::ReadingRailroad
                : rules::board::SquareType::ElectricCompany;
            const auto groupSet = ai::monopolySet(representative);
            if ((groupSet & proposals[player].propertiesGiven) != 0)
                return false;
            auto available = combinedProperties & groupSet & ~properties[player];
            if (available == 0)
                return false;
            selected = available & (0u - available);
        }
        else if (item == TradeImportanceItem::Junk)
        {
            // Retail scans for junk but then returns FALSE unconditionally.
            return false;
        }
        else if (item == TradeImportanceItem::Trade ||
                 item == TradeImportanceItem::OneUnowned ||
                 item == TradeImportanceItem::TwoUnowned)
        {
            const int expectedPossible = item == TradeImportanceItem::Trade
                ? 99
                : static_cast<int>(item) -
                    (static_cast<int>(TradeImportanceItem::OneUnowned) + 1);
            bool found{};
            for (const auto group : order)
            {
                if (player == strategyPlayer && group == 0)
                    continue;
                if ((monopoliesTraded & static_cast<std::uint16_t>(1u << group)) != 0)
                    continue;
                const auto groupSet = ai::monopolySet(ai::ExpensiveMonopolySquares[group]);
                const auto availableInGroup = combinedProperties & groupSet;
                if (availableInGroup == 0 || availableInGroup == groupSet)
                    continue;
                if (availableInGroup == (properties[player] & groupSet))
                    continue;
                const auto external = availableInGroup & ~properties[player];
                if (external == 0)
                    continue;

                if (item == TradeImportanceItem::Trade)
                {
                    auto simulatedProperties = properties;
                    for (rules::PlayerNumber current = 0;
                         current < before.numberOfPlayers; ++current)
                        simulatedProperties[current] &= ~external;
                    simulatedProperties[player] |= external;
                    const auto trade = findSmallestMonopolyTrade(
                        player, groupSet,
                        std::span<const rules::PlayerNumber>(candidates.data(), candidateCount),
                        simulatedProperties);
                    if (trade.count == 0)
                        continue;
                }
                else if (possibleMonopoly(after, player,
                             ai::ExpensiveMonopolySquares[group]) != expectedPossible)
                {
                    continue;
                }
                selected = external;
                found = true;
                break;
            }
            if (!found)
                return false;
        }
        else
        {
            return false;
        }

        proposals[player].propertiesReceived |= selected;
        for (rules::PlayerNumber current = 0; current < before.numberOfPlayers; ++current)
        {
            const auto given = selected & properties[current];
            if (given == 0)
                continue;
            proposals[current].propertiesGiven |= given;
            combinedProperties &= ~selected;
        }
        return true;
    }

    double cashMultiplier(
        double attitude,
        const CashMultiplierTable& multipliers) noexcept
    {
        if (attitude <= -1.0)
            attitude = -0.999;
        if (attitude >= 1.0)
            attitude = 0.999;

        auto index = static_cast<std::size_t>((attitude + 1.0) * 10.0);
        if (index >= WhatToTradeEntries)
            index = WhatToTradeEntries - 1;

        // Preserve retail's interpolation quirk: it uses the fractional part
        // of attitude itself, not the fractional position within the 0.1 bin.
        attitude -= std::floor(attitude);
        if (index == 0)
            return multipliers[0];
        return multipliers[index] * attitude +
            multipliers[index - 1] * (1.0 - attitude);
    }

    bool playerInvolvedInTrade(
        const TradeProposalRecord& proposal) noexcept
    {
        if (proposal.propertiesGiven != 0 || proposal.propertiesReceived != 0 ||
            proposal.cashReceived != 0 || proposal.cashGiven != 0)
            return true;
        for (std::size_t index = 0; index < DeckCount; ++index)
        {
            if (proposal.jailCardGiven[index] || proposal.jailCardReceived[index])
                return true;
        }
        return false;
    }

    rules::PlayerNumber nextPlayerWantingCash(
        const TradeProposalList& proposals) noexcept
    {
        for (rules::PlayerNumber player = 0; player < rules::MaxPlayers; ++player)
        {
            if (proposals[player].cashReceived != 0)
                return player;
        }
        return rules::NobodyPlayer;
    }

    bool tradeIsProper(
        const rules::GameState& state,
        const TradeProposalList& proposals,
        std::span<const FutureImmunityRecord> immunities) noexcept
    {
        if (state.numberOfPlayers > rules::MaxPlayers)
            return false;

        bool someoneInTrade{};
        for (rules::PlayerNumber player = 0;
             player < state.numberOfPlayers; ++player)
        {
            const auto& proposal = proposals[player];
            if (!playerInvolvedInTrade(proposal))
                continue;

            someoneInTrade = true;
            bool given = proposal.propertiesGiven != 0 || proposal.cashGiven != 0;
            bool received =
                proposal.propertiesReceived != 0 || proposal.cashReceived != 0;
            for (std::size_t index = 0; index < DeckCount; ++index)
            {
                received = received || proposal.jailCardReceived[index];
                given = given || proposal.jailCardGiven[index];
            }
            for (const auto& immunity : immunities)
            {
                if (immunity.count == 0)
                    continue;
                if (immunity.fromPlayer == player)
                    given = true;
                if (immunity.toPlayer == player)
                    received = true;
            }
            if (!given || !received)
                return false;
        }
        return someoneInTrade;
    }

    void makeTradeProper(
        const rules::GameState& state,
        TradeProposalList& proposals,
        std::span<const FutureImmunityRecord> immunities) noexcept
    {
        if (state.numberOfPlayers > rules::MaxPlayers)
            return;

        auto dollarPlayer = rules::NobodyPlayer;
        int firstPlayerNeed{};
        for (rules::PlayerNumber player = 0;
             player < state.numberOfPlayers; ++player)
        {
            auto& proposal = proposals[player];
            if (!playerInvolvedInTrade(proposal))
                continue;

            bool given = proposal.propertiesGiven != 0 || proposal.cashGiven != 0;
            bool received = proposal.propertiesReceived != 0 || proposal.cashReceived != 0;
            for (std::size_t deck = 0; deck < DeckCount; ++deck)
            {
                given = given || proposal.jailCardGiven[deck];
                received = received || proposal.jailCardReceived[deck];
            }
            for (const auto& immunity : immunities)
            {
                if (immunity.count == 0)
                    continue;
                if (immunity.fromPlayer == player)
                    given = true;
                if (immunity.toPlayer == player)
                    received = true;
            }

            if (!given)
            {
                if (dollarPlayer == rules::NobodyPlayer)
                    firstPlayerNeed = 1;
                else
                {
                    proposals[dollarPlayer].cashReceived += 1;
                    proposal.cashGiven += 1;
                }
            }
            if (!received)
            {
                if (dollarPlayer == rules::NobodyPlayer)
                    firstPlayerNeed = 2;
                else
                {
                    proposals[dollarPlayer].cashGiven += 1;
                    proposal.cashReceived += 1;
                }
            }

            if (dollarPlayer == rules::NobodyPlayer)
                dollarPlayer = player;
            else if (firstPlayerNeed != 0)
            {
                if (firstPlayerNeed == 1)
                {
                    proposals[dollarPlayer].cashGiven += 1;
                    proposal.cashReceived += 1;
                }
                else
                {
                    proposals[dollarPlayer].cashReceived += 1;
                    proposal.cashGiven += 1;
                }
            }
        }
    }

    void applyTradeToState(
        rules::GameState& state,
        const TradeProposalList& proposals) noexcept
    {
        if (state.numberOfPlayers > rules::MaxPlayers)
            return;

        for (rules::PlayerNumber player = 0;
             player < state.numberOfPlayers; ++player)
        {
            const auto& proposal = proposals[player];
            for (std::size_t index = 0;
                 index < static_cast<std::size_t>(rules::board::SquareType::InJail);
                 ++index)
            {
                const auto square = static_cast<rules::board::SquareType>(index);
                if ((rules::board::propertyBit(square) & proposal.propertiesReceived) != 0)
                    state.squares[index].owner = player;
            }
            state.players[player].cash += proposal.cashReceived - proposal.cashGiven;
            for (std::size_t deck = 0; deck < DeckCount; ++deck)
            {
                if (proposal.jailCardReceived[deck])
                    state.cards[deck].jailOwner = player;
            }
        }
    }

    bool isMonopolyTrade(
        const rules::GameState& state,
        const TradeProposalList& proposals) noexcept
    {
        if (state.numberOfPlayers > rules::MaxPlayers)
            return false;

        auto after = state;
        applyTradeToState(after, proposals);
        for (const auto square : ai::ExpensiveMonopolySquares)
        {
            const auto oldOwner = ai::isMonopoly(state, square)
                ? ai::firstOwnerInMonopoly(state, square)
                : rules::NobodyPlayer;
            const auto newOwner = ai::isMonopoly(after, square)
                ? ai::firstOwnerInMonopoly(after, square)
                : rules::NobodyPlayer;
            if (oldOwner != newOwner)
                return true;
        }
        return false;
    }

    bool playerHasFutureOrImmunity(
        rules::PlayerNumber player,
        std::span<const FutureImmunityRecord> immunities) noexcept
    {
        for (const auto& immunity : immunities)
        {
            if (immunity.count == 0)
                continue;
            if (immunity.fromPlayer == player || immunity.toPlayer == player)
                return true;
        }
        return false;
    }

    bool addTradeItem(
        TradeProposalList& proposals,
        FutureImmunityList& immunities,
        rules::TradeItemKind kind,
        std::int64_t amount,
        rules::PlayerNumber fromPlayer,
        rules::PlayerNumber toPlayer,
        rules::board::PropertySet propertySet) noexcept
    {
        bool applied{};
        const auto validPlayer = [](rules::PlayerNumber player) noexcept {
            return player < rules::MaxPlayers;
        };

        switch (kind)
        {
        case rules::TradeItemKind::Cash:
            if (validPlayer(fromPlayer) && validPlayer(toPlayer))
            {
                proposals[fromPlayer].cashGiven = amount;
                proposals[toPlayer].cashReceived = amount;
                applied = true;
            }
            break;

        case rules::TradeItemKind::Square:
            if (amount >= 0 && amount < static_cast<std::int64_t>(
                    rules::board::SquareType::InJail))
            {
                const auto square = static_cast<rules::board::SquareType>(amount);
                const auto propertyBit = rules::board::propertyBit(square);
                if (propertyBit != 0)
                {
                    if (toPlayer == rules::NobodyPlayer)
                    {
                        for (auto& proposal : proposals)
                        {
                            proposal.propertiesReceived &= ~propertyBit;
                            proposal.propertiesGiven &= ~propertyBit;
                        }
                        applied = true;
                    }
                    else if (validPlayer(fromPlayer) && validPlayer(toPlayer))
                    {
                        proposals[fromPlayer].propertiesGiven |= propertyBit;
                        proposals[toPlayer].propertiesReceived |= propertyBit;
                        applied = true;
                    }
                }
            }
            break;

        case rules::TradeItemKind::JailCard:
            if (amount >= 0 && amount < static_cast<std::int64_t>(DeckCount))
            {
                const auto deck = static_cast<std::size_t>(amount);
                if (toPlayer == rules::NobodyPlayer)
                {
                    for (auto& proposal : proposals)
                    {
                        proposal.jailCardGiven[deck] = false;
                        proposal.jailCardReceived[deck] = false;
                    }
                    applied = true;
                }
                else if (validPlayer(fromPlayer) && validPlayer(toPlayer))
                {
                    proposals[fromPlayer].jailCardGiven[deck] = true;
                    proposals[toPlayer].jailCardReceived[deck] = true;
                    applied = true;
                }
            }
            break;

        case rules::TradeItemKind::Immunity:
        case rules::TradeItemKind::FutureRent:
            if (validPlayer(fromPlayer) && validPlayer(toPlayer))
            {
                const auto count = static_cast<std::uint8_t>(amount);
                auto existing = std::find_if(
                    immunities.begin(), immunities.end(),
                    [&](const FutureImmunityRecord& entry) {
                        return entry.count != 0 &&
                            entry.hitType == kind &&
                            entry.properties == propertySet &&
                            entry.toPlayer == toPlayer;
                    });
                if (existing != immunities.end())
                {
                    existing->count = count;
                    applied = true;
                }
                else
                {
                    auto free = std::find_if(
                        immunities.begin(), immunities.end(),
                        [](const FutureImmunityRecord& entry) {
                            return entry.count == 0;
                        });
                    if (free != immunities.end())
                    {
                        *free = {propertySet, fromPlayer, toPlayer, count, kind};
                        applied = true;
                    }
                }
            }
            break;
        }

        // The retail loop forgot to advance list_point and normalized player 0
        // repeatedly. Normalize every proposal as the comment intended.
        for (auto& proposal : proposals)
        {
            if (proposal.cashGiven > proposal.cashReceived)
            {
                proposal.cashGiven -= proposal.cashReceived;
                proposal.cashReceived = 0;
            }
            else
            {
                proposal.cashReceived -= proposal.cashGiven;
                proposal.cashGiven = 0;
            }
        }
        return applied;
    }

}
