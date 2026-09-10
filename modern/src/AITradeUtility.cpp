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

}
