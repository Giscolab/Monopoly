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
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
