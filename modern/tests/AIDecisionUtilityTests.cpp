#include "AIDecisionUtility.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;
    using rules::board::SquareType;

    void require(bool condition, const char* description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << description << '\n';
        if (!condition) throw std::runtime_error(description);
    }

    void own(rules::GameState& state, SquareType square, rules::PlayerNumber player)
    {
        state.squares[static_cast<std::size_t>(square)].owner = player;
    }

    rules::GameState baseState()
    {
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.options.housesPerHotel = 5;
        state.options.evenBuildRule = true;
        return state;
    }

    void testRetailConstants()
    {
        using ai::decision::CashStrategy;
        require(static_cast<int>(CashStrategy::MinimumAmount) == 0 &&
                static_cast<int>(CashStrategy::ExpectedRentIncludingMonopolies) == 1 &&
                static_cast<int>(CashStrategy::ExpectedRentExcludingMonopolies) == 2 &&
                static_cast<int>(CashStrategy::HighestRent) == 3 &&
                static_cast<int>(CashStrategy::MonopolyDependent) == 4,
            "AI cash strategies preserve retail numeric contract");
        require(ai::decision::MonopolyAverageLandingFrequency[3] == 1.103 &&
                ai::decision::MonopolyAverageLandingFrequency[7] == 0.890,
            "AI monopoly landing-frequency priorities preserve retail constants");
    }

    void testExcessCashStrategies()
    {
        using ai::decision::CashStrategy;
        auto state = baseState();
        state.players[0].cash = 500;
        own(state, SquareType::ReadingRailroad, 0);
        require(ai::decision::excessCashAvailable(
                    state, 0, false, CashStrategy::MinimumAmount, 200) == 400,
            "AI excess cash includes mortgageable non-monopoly assets");
        require(ai::decision::excessCashAvailable(
                    state, 0, false, CashStrategy::MinimumAmount, 200, 50) == 350,
            "AI excess cash subtracts explicitly injected AI debt");
        require(ai::decision::excessCashAvailable(
                    state, 0, true, CashStrategy::MinimumAmount, 200, 450) == 300,
            "AI cash-only mode preserves retail raw-cash override including debt omission");
        require(ai::decision::excessCashAvailable(
                    state, 0, false, CashStrategy::MinimumAmount, 900) == 0,
            "AI excess cash clamps negative result to zero");

        own(state, SquareType::ParkPlace, 1);
        own(state, SquareType::Boardwalk, 1);
        const auto liquid = ai::liquidAssets(state, 0, false, false, 0);
        const auto expectedWith = ai::averageRentPaid(state, 0, 0, true, 1.0);
        const auto expectedWithout = ai::averageRentPaid(state, 0, 0, false, 1.0);
        require(ai::decision::excessCashAvailable(
                    state, 0, false, CashStrategy::ExpectedRentIncludingMonopolies, 25) ==
                std::max<std::int64_t>(0, liquid - std::max<std::int64_t>(25, expectedWith)),
            "AI expected-rent cash strategy includes developed-monopoly exposure flag");
        require(ai::decision::excessCashAvailable(
                    state, 0, false, CashStrategy::ExpectedRentExcludingMonopolies, 25) ==
                std::max<std::int64_t>(0, liquid - std::max<std::int64_t>(25, expectedWithout)),
            "AI expected-rent cash strategy excludes developed monopolies when configured");

        state.squares[static_cast<std::size_t>(SquareType::ParkPlace)].houses = 1;
        state.squares[static_cast<std::size_t>(SquareType::Boardwalk)].houses = 1;
        const auto highest = ai::highestRentSquare(state, 0);
        require(highest.square == SquareType::Boardwalk && highest.rent > 0,
            "AI highest-rent cash fixture exposes developed opponent monopoly");
        const auto highReserve = highest.rent +
            ai::averageRentPaid(state, 0, 0, false, 1.0);
        require(ai::decision::excessCashAvailable(
                    state, 0, false, CashStrategy::HighestRent, 0) ==
                std::max<std::int64_t>(0, liquid - highReserve),
            "AI highest-rent cash strategy reserves developed worst-case rent plus base expenses");
        require(ai::decision::excessCashAvailable(
                    state, 2, false, CashStrategy::MinimumAmount, 0) == 0,
            "AI excess-cash decision rejects invalid player safely");
    }

    void testHypotheticalBuilding()
    {
        auto brown = baseState();
        brown.players[0].cash = 1000;
        own(brown, SquareType::MediterraneanAvenue, 0);
        own(brown, SquareType::BalticAvenue, 0);
        require(ai::decision::hypotheticalBuyHouse(brown, 0),
            "AI hypothetical builder buys first affordable monopoly house");
        require(brown.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].houses == 1 &&
                brown.players[0].cash == 950,
            "AI hypothetical builder starts on highest-rent lot and debits house price");
        require(ai::decision::hypotheticalBuyHouse(brown, 0),
            "AI hypothetical builder can continue same monopoly");
        require(brown.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].houses == 1,
            "AI hypothetical builder preserves even-build rule");

        auto frequency = baseState();
        frequency.players[0].cash = 200;
        for (const auto square : {SquareType::MediterraneanAvenue, SquareType::BalticAvenue,
                 SquareType::StJamesPlace, SquareType::TennesseeAvenue, SquareType::NewYorkAvenue})
            own(frequency, square, 0);
        require(ai::decision::hypotheticalBuyHouse(frequency, 0),
            "AI hypothetical builder falls back when no monopoly can reach three houses");
        require(frequency.squares[static_cast<std::size_t>(SquareType::NewYorkAvenue)].houses == 1 &&
                frequency.players[0].cash == 100,
            "AI fallback selects orange by retail landing frequency");

        auto multiple = baseState();
        multiple.players[0].cash = 2000;
        for (const auto square : {SquareType::MediterraneanAvenue, SquareType::BalticAvenue,
                 SquareType::ParkPlace, SquareType::Boardwalk})
            own(multiple, square, 0);
        require(ai::decision::hypotheticalBuyHouse(multiple, 0),
            "AI hypothetical builder chooses highest-base-rent fully fundable monopoly");
        require(multiple.squares[static_cast<std::size_t>(SquareType::Boardwalk)].houses == 1,
            "AI hypothetical builder selects dark-blue property over earlier brown monopoly");
        require(multiple.players[0].cash == 1800,
            "AI hypothetical builder fixes retail monopolies[0] house-cost debit bug");

        auto mortgaged = baseState();
        mortgaged.players[0].cash = 1000;
        own(mortgaged, SquareType::MediterraneanAvenue, 0);
        own(mortgaged, SquareType::BalticAvenue, 0);
        mortgaged.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].mortgaged = true;
        require(!ai::decision::hypotheticalBuyHouse(mortgaged, 0),
            "AI hypothetical builder excludes mortgaged monopoly like retail Get_Monopolies TRUE");

        auto broke = baseState();
        own(broke, SquareType::MediterraneanAvenue, 0);
        own(broke, SquareType::BalticAvenue, 0);
        require(!ai::decision::hypotheticalBuyHouse(broke, 0),
            "AI hypothetical builder refuses zero liquid assets");
    }
}

int main()
{
    try
    {
        require(monopoly::rules::board::initializeForOptions(monopoly::rules::GameOptions{}),
            "AI decision fixture initializes retail board definitions");
        testRetailConstants();
        testExcessCashStrategies();
        testHypotheticalBuilding();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
