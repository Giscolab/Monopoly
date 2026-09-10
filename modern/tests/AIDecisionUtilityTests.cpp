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
        using ai::decision::HousePurchaseDecision;
        require(static_cast<int>(HousePurchaseDecision::No) == 0 &&
                static_cast<int>(HousePurchaseDecision::Yes) == 1 &&
                static_cast<int>(HousePurchaseDecision::Later) == 2,
            "AI house purchase decision preserves retail EBOOL numeric contract");
        require(ai::decision::HouseBuyAtLeast3 == 1 &&
                ai::decision::HouseBuyWithin12 == 2,
            "AI housing purchase flags preserve retail bit contract");
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
    void testShouldUnmortgageAndBuyHouse()
    {
        using ai::decision::CashStrategy;
        using ai::decision::HousePurchaseDecision;

        auto nonMonopoly = baseState();
        nonMonopoly.players[0].cash = 100;
        own(nonMonopoly, SquareType::ReadingRailroad, 0);
        own(nonMonopoly, SquareType::PennsylvaniaRailroad, 0);
        nonMonopoly.squares[static_cast<std::size_t>(SquareType::ReadingRailroad)].mortgaged = true;
        require(!ai::decision::shouldUnmortgageProperty(
                    nonMonopoly, 0, CashStrategy::MinimumAmount, 50),
            "AI should-unmortgage uses cash-only reserve for non-monopoly property");
        nonMonopoly.players[0].cash = 160;
        require(ai::decision::shouldUnmortgageProperty(
                    nonMonopoly, 0, CashStrategy::MinimumAmount, 50),
            "AI should-unmortgage accepts non-monopoly once cash-only reserve covers 110 percent");

        auto monopoly = baseState();
        monopoly.players[0].cash = 0;
        own(monopoly, SquareType::MediterraneanAvenue, 0);
        own(monopoly, SquareType::BalticAvenue, 0);
        own(monopoly, SquareType::ReadingRailroad, 0);
        monopoly.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].mortgaged = true;
        require(ai::decision::shouldUnmortgageProperty(
                    monopoly, 0, CashStrategy::MinimumAmount, 0),
            "AI should-unmortgage uses mortgageable excess for monopoly property");

        auto buildFirst = baseState();
        buildFirst.players[0].cash = 300;
        own(buildFirst, SquareType::ParkPlace, 0);
        own(buildFirst, SquareType::Boardwalk, 0);
        own(buildFirst, SquareType::ReadingRailroad, 0);
        buildFirst.squares[static_cast<std::size_t>(SquareType::ReadingRailroad)].mortgaged = true;
        require(!ai::decision::shouldUnmortgageProperty(
                    buildFirst, 0, CashStrategy::MinimumAmount, 0),
            "AI should-unmortgage simulates desired house purchase before freeing non-monopoly mortgage");
        buildFirst.options.maximumHouses = ai::decision::CriticalHousingLevel;
        require(ai::decision::shouldUnmortgageProperty(
                    buildFirst, 0, CashStrategy::MinimumAmount, 0),
            "AI should-unmortgage skips hypothetical housing during retail critical shortage");

        auto noMonopoly = baseState();
        noMonopoly.players[0].cash = 1000;
        require(ai::decision::shouldBuyHouse(
                    noMonopoly, 0, CashStrategy::MinimumAmount, 0, 0) ==
                HousePurchaseDecision::No,
            "AI should-buy-house rejects player without monopoly");

        auto brown = baseState();
        brown.players[0].cash = 100;
        own(brown, SquareType::MediterraneanAvenue, 0);
        own(brown, SquareType::BalticAvenue, 0);
        require(ai::decision::shouldBuyHouse(
                    brown, 0, CashStrategy::MinimumAmount, 0, 0) ==
                HousePurchaseDecision::Yes,
            "AI should-buy-house accepts affordable monopoly house");

        auto atLeastThree = brown;
        atLeastThree.players[0].cash = 250;
        require(ai::decision::shouldBuyHouse(
                    atLeastThree, 0, CashStrategy::MinimumAmount, 0,
                    ai::decision::HouseBuyAtLeast3) == HousePurchaseDecision::No,
            "AI at-least-three housing strategy reserves full six-house brown build");
        atLeastThree.players[0].cash = 300;
        require(ai::decision::shouldBuyHouse(
                    atLeastThree, 0, CashStrategy::MinimumAmount, 0,
                    ai::decision::HouseBuyAtLeast3) == HousePurchaseDecision::Yes,
            "AI at-least-three housing strategy accepts exact full-build budget");

        auto proximity = brown;
        proximity.numberOfPlayers = 2;
        proximity.players[0].cash = 500;
        proximity.players[1].currentSquare = static_cast<std::uint8_t>(SquareType::FreeParking);
        require(ai::decision::shouldBuyHouse(
                    proximity, 0, CashStrategy::MinimumAmount, 0,
                    ai::decision::HouseBuyWithin12) == HousePurchaseDecision::Later,
            "AI within-12 housing strategy returns Later when monopoly is affordable but opponents are far");
        proximity.players[1].currentSquare = static_cast<std::uint8_t>(SquareType::Boardwalk);
        require(ai::decision::shouldBuyHouse(
                    proximity, 0, CashStrategy::MinimumAmount, 0,
                    ai::decision::HouseBuyWithin12) == HousePurchaseDecision::Yes,
            "AI within-12 housing strategy buys when opponent approaches monopoly");
        proximity.players[1].currentSquare = static_cast<std::uint8_t>(SquareType::FreeParking);
        proximity.options.maximumHouses = ai::decision::CriticalHousingLevel;
        require(ai::decision::shouldBuyHouse(
                    proximity, 0, CashStrategy::MinimumAmount, 0,
                    ai::decision::HouseBuyWithin12) == HousePurchaseDecision::Yes,
            "AI housing shortage ignores within-12 delay like retail");

        auto mortgagedBrown = brown;
        mortgagedBrown.players[0].cash = 82;
        mortgagedBrown.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].mortgaged = true;
        require(ai::decision::shouldBuyHouse(
                    mortgagedBrown, 0, CashStrategy::MinimumAmount, 0, 0) ==
                HousePurchaseDecision::No,
            "AI should-buy-house subtracts monopoly unmortgage cost before house budget");
        mortgagedBrown.players[0].cash = 83;
        require(ai::decision::shouldBuyHouse(
                    mortgagedBrown, 0, CashStrategy::MinimumAmount, 0, 0) ==
                HousePurchaseDecision::Yes,
            "AI should-buy-house accepts exact truncated unmortgage-plus-house budget");
    }

    void testHypotheticalUnmortgageAndGiveAway()
    {
        using ai::decision::CashStrategy;
        auto unmortgage = baseState();
        unmortgage.players[0].cash = 1000;
        for (const auto square : {SquareType::MediterraneanAvenue, SquareType::BalticAvenue,
                 SquareType::OrientalAvenue, SquareType::VermontAvenue, SquareType::ConnecticutAvenue})
            own(unmortgage, square, 0);
        unmortgage.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].mortgaged = true;
        unmortgage.squares[static_cast<std::size_t>(SquareType::OrientalAvenue)].mortgaged = true;
        require(ai::decision::hypotheticalUnmortgageMonopolyProperty(
                    unmortgage, 0, CashStrategy::MinimumAmount, 0) == SquareType::OrientalAvenue,
            "AI hypothetical unmortgage scans non-brown monopolies before brown like retail");
        require(!unmortgage.squares[static_cast<std::size_t>(SquareType::OrientalAvenue)].mortgaged &&
                unmortgage.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].mortgaged,
            "AI hypothetical unmortgage mutates only selected property");
        require(unmortgage.players[0].cash == 945,
            "AI hypothetical unmortgage debits truncated 110-percent Oriental cost");
        require(ai::decision::hypotheticalUnmortgageMonopolyProperty(
                    unmortgage, 0, CashStrategy::MinimumAmount, 0) == SquareType::MediterraneanAvenue,
            "AI hypothetical unmortgage falls back to brown monopoly after later groups");

        auto floatingEdge = baseState();
        floatingEdge.players[0].cash = 220;
        own(floatingEdge, SquareType::ParkPlace, 0);
        own(floatingEdge, SquareType::Boardwalk, 0);
        floatingEdge.squares[static_cast<std::size_t>(SquareType::Boardwalk)].mortgaged = true;
        require(ai::decision::hypotheticalUnmortgageMonopolyProperty(
                    floatingEdge, 0, CashStrategy::MinimumAmount, 0) == SquareType::Go,
            "AI hypothetical unmortgage preserves retail floating 110-percent exact-ceiling rejection");
        ++floatingEdge.players[0].cash;
        require(ai::decision::hypotheticalUnmortgageMonopolyProperty(
                    floatingEdge, 0, CashStrategy::MinimumAmount, 0) == SquareType::Boardwalk &&
                floatingEdge.players[0].cash == 1,
            "AI hypothetical unmortgage accepts one-dollar-above floating ceiling and truncates debit");

        auto noMonopoly = baseState();
        noMonopoly.numberOfPlayers = 3;
        noMonopoly.players[1].cash = 500;
        require(!ai::decision::shouldGiveAwayMonopoly(
                    noMonopoly, 0, CashStrategy::MinimumAmount, 0, 3),
            "AI monopoly giveaway rejects player without monopoly");

        auto shortage = baseState();
        shortage.numberOfPlayers = 3;
        shortage.options.maximumHouses = ai::decision::CriticalHousingLevel;
        shortage.players[1].cash = 500;
        own(shortage, SquareType::MediterraneanAvenue, 0);
        own(shortage, SquareType::BalticAvenue, 0);
        own(shortage, SquareType::ParkPlace, 2);
        own(shortage, SquareType::Boardwalk, 2);
        require(!ai::decision::shouldGiveAwayMonopoly(
                    shortage, 0, CashStrategy::MinimumAmount, 0, 3),
            "AI monopoly giveaway rejects inclusive retail critical housing shortage");

        auto giveaway = baseState();
        giveaway.numberOfPlayers = 3;
        giveaway.players[0].cash = 100;
        giveaway.players[1].cash = 500;
        own(giveaway, SquareType::MediterraneanAvenue, 0);
        own(giveaway, SquareType::BalticAvenue, 0);
        own(giveaway, SquareType::ParkPlace, 2);
        own(giveaway, SquareType::Boardwalk, 2);
        require(ai::decision::shouldGiveAwayMonopoly(
                    giveaway, 0, CashStrategy::MinimumAmount, 0, 3),
            "AI monopoly giveaway survives simulation when owned monopoly remains lightly developed");

        auto developed = giveaway;
        developed.players[0].cash = 0;
        developed.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].houses = 4;
        developed.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].houses = 4;
        require(!ai::decision::shouldGiveAwayMonopoly(
                    developed, 0, CashStrategy::MinimumAmount, 0, 3),
            "AI monopoly giveaway rejects monopoly above configured houses-per-square threshold");
        require(ai::decision::shouldGiveAwayMonopoly(
                    developed, 0, CashStrategy::MinimumAmount, 0, 4),
            "AI monopoly giveaway preserves inclusive houses-per-square threshold");

        auto soleOwner = giveaway;
        soleOwner.squares[static_cast<std::size_t>(SquareType::ParkPlace)].owner = rules::NobodyPlayer;
        soleOwner.squares[static_cast<std::size_t>(SquareType::Boardwalk)].owner = rules::NobodyPlayer;
        require(!ai::decision::shouldGiveAwayMonopoly(
                    soleOwner, 0, CashStrategy::MinimumAmount, 0, 3),
            "AI monopoly giveaway rejects sole monopoly owner when opponents cannot trade for one");
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
        testShouldUnmortgageAndBuyHouse();
        testHypotheticalUnmortgageAndGiveAway();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
