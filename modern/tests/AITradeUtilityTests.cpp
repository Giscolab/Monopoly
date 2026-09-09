#include "AITradeUtility.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;
    using rules::board::SquareType;

    void require(bool condition, const char* description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << description << '\n';
        if (!condition) throw std::runtime_error(description);
    }

    rules::board::PropertySet bits(
        std::initializer_list<SquareType> squares)
    {
        rules::board::PropertySet result{};
        for (const auto square : squares)
            result |= rules::board::propertyBit(square);
        return result;
    }

    bool contains(
        const ai::trade::MonopolyTradeGroup& group,
        rules::PlayerNumber player)
    {
        for (std::size_t index = 0; index < group.count; ++index)
        {
            if (group.players[index] == player)
                return true;
        }
        return false;
    }

    void testBasicContracts()
    {
        ai::trade::PropertySets properties{};
        const std::array<rules::PlayerNumber, 0> none{};
        require(ai::trade::findSmallestMonopolyTrade(
                    0, ai::monopolySet(SquareType::MediterraneanAvenue),
                    none, properties).count == 0,
            "AI trade search rejects empty candidate list like retail");

        properties[0] = bits({SquareType::MediterraneanAvenue});
        properties[1] = bits({SquareType::BalticAvenue});
        const std::array<rules::PlayerNumber, 1> one{1};
        require(ai::trade::findSmallestMonopolyTrade(
                    0, ai::monopolySet(SquareType::MediterraneanAvenue),
                    one, properties).count == 0,
            "AI trade search requires enough monopolies for every participant");
    }

    void testSingleAndMinimalGroups()
    {
        ai::trade::PropertySets properties{};
        properties[0] = bits({SquareType::MediterraneanAvenue,
            SquareType::ParkPlace});
        properties[1] = bits({SquareType::BalticAvenue,
            SquareType::Boardwalk});
        properties[2] = bits({SquareType::OrientalAvenue});

        const std::array<rules::PlayerNumber, 1> one{1};
        const auto single = ai::trade::findSmallestMonopolyTrade(
            0, ai::monopolySet(SquareType::MediterraneanAvenue),
            one, properties);
        require(single.count == 1 && single.players[0] == 1,
            "AI trade search accepts one-player balanced monopoly trade");

        const std::array<rules::PlayerNumber, 2> candidates{2, 1};
        const auto smallest = ai::trade::findSmallestMonopolyTrade(
            0, ai::monopolySet(SquareType::MediterraneanAvenue),
            candidates, properties);
        require(smallest.count == 1 && contains(smallest, 1),
            "AI trade recursion finds smaller valid subset independent of prefix");
    }

    void testTwoPlayerRequirement()
    {
        ai::trade::PropertySets properties{};
        properties[0] = bits({SquareType::MediterraneanAvenue,
            SquareType::ParkPlace, SquareType::OrientalAvenue});
        properties[1] = bits({SquareType::BalticAvenue,
            SquareType::VermontAvenue});
        properties[2] = bits({SquareType::Boardwalk,
            SquareType::ConnecticutAvenue});

        const std::array<rules::PlayerNumber, 2> candidates{1, 2};
        const auto group = ai::trade::findSmallestMonopolyTrade(
            0, ai::monopolySet(SquareType::MediterraneanAvenue),
            candidates, properties);
        require(group.count == 2 && contains(group, 1) && contains(group, 2),
            "AI trade recursion keeps both players when three monopolies require them");

        properties[1] &= ~rules::board::propertyBit(SquareType::BalticAvenue);
        require(ai::trade::findSmallestMonopolyTrade(
                    0, ai::monopolySet(SquareType::MediterraneanAvenue),
                    candidates, properties).count == 0,
            "AI trade search rejects groups that cannot complete requested monopoly");
    }

    void testInputGuards()
    {
        ai::trade::PropertySets properties{};
        const std::array<rules::PlayerNumber, 1> invalid{rules::NobodyPlayer};
        require(ai::trade::findSmallestMonopolyTrade(
                    rules::NobodyPlayer, 0, invalid, properties).count == 0,
            "AI trade search rejects invalid requesting player");
        require(ai::trade::findSmallestMonopolyTrade(
                    0, 0, invalid, properties).count == 0,
            "AI trade search rejects invalid candidate player");
    }

    void fillStaticNoMonopolyBoard(rules::GameState& state)
    {
        state.numberOfPlayers = 2;
        state.options.passingGoAmount = 200;
        for (std::size_t groupIndex = 0; groupIndex < 8; ++groupIndex)
        {
            const auto group = static_cast<rules::board::SquareGroup>(groupIndex);
            const auto range = ai::groupRange(group);
            std::size_t ordinal{};
            for (std::size_t index = range.begin; index < range.endExclusive; ++index)
            {
                const auto square = static_cast<SquareType>(index);
                if (rules::board::definition(square).group != group)
                    continue;
                state.squares[index].owner =
                    static_cast<rules::PlayerNumber>(ordinal++ % 2);
            }
        }
    }

    void testTransferAndPossibleMonopolyContracts()
    {
        rules::GameState state{};
        state.options.taxRate = 10;
        state.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].mortgaged = true;
        state.squares[static_cast<std::size_t>(SquareType::ReadingRailroad)].mortgaged = true;
        const auto taxed = bits({SquareType::MediterraneanAvenue,
            SquareType::BalticAvenue, SquareType::ReadingRailroad});
        require(ai::trade::transferTax(state, taxed) == 26,
            "AI transfer tax charges only mortgaged properties at purchase-cost rate");
        state.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].mortgaged = false;
        state.squares[static_cast<std::size_t>(SquareType::ReadingRailroad)].mortgaged = false;
        require(ai::trade::transferTax(state, taxed) == 0,
            "AI transfer tax ignores unmortgaged properties");

        require(ai::trade::vetoMonopolies(bits({SquareType::MediterraneanAvenue,
                    SquareType::BalticAvenue})) == 0,
            "AI veto count preserves retail omission of brown monopoly");
        require(ai::trade::vetoMonopolies(bits({SquareType::OrientalAvenue,
                    SquareType::ParkPlace})) == 2,
            "AI veto count counts distinct Oriental-through-Park-Place groups");

        state.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].owner = 0;
        require(ai::trade::possibleMonopoly(
                    state, 0, SquareType::MediterraneanAvenue) == 1,
            "AI possible-monopoly counts one unowned brown lot");
        state.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].owner = 0;
        require(ai::trade::possibleMonopoly(
                    state, 0, SquareType::MediterraneanAvenue) == 0,
            "AI possible-monopoly returns zero for already complete group");
        state.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].owner = 1;
        require(ai::trade::possibleMonopoly(
                    state, 0, SquareType::MediterraneanAvenue) == -1,
            "AI possible-monopoly rejects group blocked by another owner");

        rules::GameState rail{};
        rail.squares[static_cast<std::size_t>(SquareType::ReadingRailroad)].owner = 0;
        require(ai::trade::possibleMonopoly(
                    rail, 0, SquareType::ReadingRailroad) == 3,
            "AI possible-monopoly preserves sparse railroad scan contract");
        require(ai::trade::possibleMonopoly(
                    rail, rules::NobodyPlayer, SquareType::ReadingRailroad) == -1,
            "AI possible-monopoly rejects invalid owner safely");
        require(ai::trade::possibleMonopoly(
                    rail, 0, SquareType::Chance1) == -1,
            "AI possible-monopoly rejects non-property groups safely");
    }

    void testShouldTradeForMonopoly()
    {
        using Decision = ai::trade::MonopolyTradeDecision;
        require(static_cast<int>(Decision::Avoid) == 0 &&
                static_cast<int>(Decision::Required) == 1 &&
                static_cast<int>(Decision::Maybe) == 2,
            "AI monopoly-trade decision preserves retail FALSE/TRUE/MAYBE values");

        rules::GameState buying{};
        buying.numberOfPlayers = 2;
        require(ai::trade::shouldTradeForMonopoly(buying, 0) == Decision::Avoid,
            "AI monopoly trade waits during retail buying stage");
        require(ai::trade::shouldTradeForMonopoly(buying, 2) == Decision::Avoid,
            "AI monopoly trade rejects invalid requesting player safely");

        rules::GameState monopoly{};
        monopoly.numberOfPlayers = 2;
        monopoly.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].owner = 0;
        monopoly.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].owner = 0;
        require(ai::trade::shouldTradeForMonopoly(monopoly, 0) == Decision::Avoid,
            "AI monopoly owner avoids forced monopoly trade");
        require(ai::trade::shouldTradeForMonopoly(monopoly, 1) == Decision::Required,
            "AI player without monopoly requires trade once monopoly exists");

        rules::GameState leader{};
        fillStaticNoMonopolyBoard(leader);
        for (const auto square : {SquareType::ReadingRailroad,
                 SquareType::PennsylvaniaRailroad, SquareType::BAndORailroad,
                 SquareType::ShortLineRailroad})
            leader.squares[static_cast<std::size_t>(square)].owner = 0;
        require(ai::trade::shouldTradeForMonopoly(leader, 0) == Decision::Maybe,
            "AI top earner can wait for a preferred monopoly trade");

        rules::GameState lagging{};
        fillStaticNoMonopolyBoard(lagging);
        for (const auto square : {SquareType::ReadingRailroad,
                 SquareType::PennsylvaniaRailroad, SquareType::BAndORailroad,
                 SquareType::ShortLineRailroad})
            lagging.squares[static_cast<std::size_t>(square)].owner = 1;
        require(ai::trade::shouldTradeForMonopoly(lagging, 0) == Decision::Required,
            "AI income lag above twenty percent requires monopoly trade");
    }
}

int main()
{
    try
    {
        require(rules::board::initializeForOptions(rules::GameOptions{}),
            "AI trade fixture initializes retail board definitions");
        testBasicContracts();
        testSingleAndMinimalGroups();
        testTwoPlayerRequirement();
        testInputGuards();
        testTransferAndPossibleMonopolyContracts();
        testShouldTradeForMonopoly();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
