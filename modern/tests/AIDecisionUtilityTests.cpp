#include "AIDecisionUtility.hpp"

#include <cmath>
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

    void testCashAvailableAfterHousing()
    {
        using ai::decision::CashStrategy;
        auto state = baseState();
        state.players[0].cash = 150;
        own(state, SquareType::MediterraneanAvenue, 0);
        own(state, SquareType::BalticAvenue, 0);
        require(ai::decision::cashAvailableAfterHousing(
                    state, 0, CashStrategy::MinimumAmount, 0) == 0,
            "AI cash-after-housing spends affordable brown development first");

        state.options.maximumHouses = ai::decision::CriticalHousingLevel;
        require(ai::decision::cashAvailableAfterHousing(
                    state, 0, CashStrategy::MinimumAmount, 0) == 150,
            "AI cash-after-housing skips building during retail critical shortage");
        require(ai::decision::cashAvailableAfterHousing(
                    state, 2, CashStrategy::MinimumAmount, 0) == 0,
            "AI cash-after-housing rejects invalid player safely");
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
        nonMonopoly.players[0].cash = 161;
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

    void testEconomicActionPlanning()
    {
        using ai::decision::CashStrategy;
        using ai::decision::EconomicActionKind;

        auto brown = baseState();
        brown.players[0].cash = 100;
        own(brown, SquareType::MediterraneanAvenue, 0);
        own(brown, SquareType::BalticAvenue, 0);
        auto plan = ai::decision::planBuyHouseAction(
            brown, 0, CashStrategy::MinimumAmount, 0, 0);
        require(plan.kind == EconomicActionKind::BuyHouse &&
                plan.square == SquareType::BalticAvenue,
            "AI economic planner builds first on the highest-rent legal brown lot");

        auto mortgagedBrown = brown;
        mortgagedBrown.players[0].cash = 100;
        mortgagedBrown.squares[
            static_cast<std::size_t>(SquareType::MediterraneanAvenue)].mortgaged = true;
        plan = ai::decision::planBuyHouseAction(
            mortgagedBrown, 0, CashStrategy::MinimumAmount, 0, 0);
        require(plan.kind == EconomicActionKind::UnmortgageProperty &&
                plan.square == SquareType::MediterraneanAvenue,
            "AI house planner unmortgages selected monopoly before building");

        auto financeMonopoly = baseState();
        financeMonopoly.players[0].cash = 0;
        own(financeMonopoly, SquareType::MediterraneanAvenue, 0);
        own(financeMonopoly, SquareType::BalticAvenue, 0);
        own(financeMonopoly, SquareType::ReadingRailroad, 0);
        financeMonopoly.squares[
            static_cast<std::size_t>(SquareType::MediterraneanAvenue)].mortgaged = true;
        plan = ai::decision::planUnmortgagePropertyAction(
            financeMonopoly, 0, CashStrategy::MinimumAmount, 0);
        require(plan.kind == EconomicActionKind::MortgageProperty &&
                plan.square == SquareType::ReadingRailroad,
            "AI unmortgage planner first raises cash from worst non-monopoly asset");

        auto nonMonopoly = baseState();
        nonMonopoly.players[0].cash = 161;
        own(nonMonopoly, SquareType::ReadingRailroad, 0);
        own(nonMonopoly, SquareType::PennsylvaniaRailroad, 0);
        nonMonopoly.squares[
            static_cast<std::size_t>(SquareType::ReadingRailroad)].mortgaged = true;
        plan = ai::decision::planUnmortgagePropertyAction(
            nonMonopoly, 0, CashStrategy::MinimumAmount, 50);
        require(plan.kind == EconomicActionKind::UnmortgageProperty &&
                plan.square == SquareType::ReadingRailroad,
            "AI unmortgage planner selects highest-rent affordable mortgaged property");

        auto proximity = brown;
        proximity.players[0].cash = 500;
        proximity.players[1].currentSquare =
            static_cast<std::uint8_t>(SquareType::FreeParking);
        plan = ai::decision::planBuyHouseAction(
            proximity, 0, CashStrategy::MinimumAmount, 0,
            ai::decision::HouseBuyWithin12);
        require(!plan.acted(),
            "AI house action planner preserves retail within-12 execution gate");
        proximity.options.maximumHouses = ai::decision::CriticalHousingLevel;
        plan = ai::decision::planBuyHouseAction(
            proximity, 0, CashStrategy::MinimumAmount, 0,
            ai::decision::HouseBuyWithin12);
        require(!plan.acted(),
            "AI house execution retains within-12 gate during shortage like AI_Buy_Houses retail");
    }

    void testTaxDecision()
    {
        auto state = baseState();
        state.options.taxRate = 10;
        state.options.flatTaxFee = 200;
        state.players[0].cash = 2009;
        require(ai::decision::chooseFractionTax(state, 0) == true,
            "AI tax decision preserves retail integer truncation at flat-fee boundary");
        state.players[0].cash = 2010;
        require(ai::decision::chooseFractionTax(state, 0) == false,
            "AI tax decision selects flat fee once percentage tax exceeds it");
        require(!ai::decision::chooseFractionTax(state, 2).has_value(),
            "AI tax decision rejects invalid player safely");
    }

    void testJailExitChoice()
    {
        using ai::decision::JailExitChoice;

        auto state = baseState();
        state.players[0].turnsInJail = 2;
        require(ai::decision::chooseJailExitChoice(
                    state, 0, true, true, true) == JailExitChoice::Card,
            "AI jail choice uses a card on the retail third turn");

        state.players[0].turnsInJail = 0;
        require(ai::decision::chooseJailExitChoice(
                    state, 0, false, false, false) == JailExitChoice::Pay,
            "AI jail choice preserves retail ignored can-pay flag when rolling is unavailable");
        require(ai::decision::chooseJailExitChoice(
                    state, 0, false, false, true) == JailExitChoice::Card,
            "AI jail choice prefers card when rolling is unavailable");

        auto monopoly = baseState();
        monopoly.options.passingGoAmount = 1000;
        own(monopoly, SquareType::StCharlesPlace, 1);
        own(monopoly, SquareType::StatesAvenue, 1);
        require(ai::decision::chooseJailExitChoice(
                    monopoly, 0, true, true, false) == JailExitChoice::Roll,
            "AI jail choice stays for Virginia when it gives a direct monopoly");

        auto cheapStay = baseState();
        cheapStay.options.passingGoAmount = 0;
        cheapStay.options.getOutOfJailFee = 50;
        require(ai::decision::chooseJailExitChoice(
                    cheapStay, 0, true, true, false) == JailExitChoice::Roll,
            "AI jail choice stays when expected saved exposure is below jail fee");

        auto leave = baseState();
        leave.options.passingGoAmount = 1000;
        leave.options.getOutOfJailFee = 50;
        require(ai::decision::chooseJailExitChoice(
                    leave, 0, true, true, false) == JailExitChoice::Pay,
            "AI jail choice pays when leaving is economically preferable");
        require(ai::decision::chooseJailExitChoice(
                    leave, 0, true, true, true) == JailExitChoice::Card,
            "AI jail choice uses card instead of cash when choosing to leave");
        require(!ai::decision::chooseJailExitChoice(
                    leave, 2, true, true, false).has_value(),
            "AI jail choice rejects invalid player safely");
    }

    void testWorthFactorsAndMortgageWorstProperty()
    {
        using ai::decision::WorthFactors;
        auto worth = baseState();
        worth.players[0].cash = 123;
        own(worth, SquareType::MediterraneanAvenue, 0);
        own(worth, SquareType::ReadingRailroad, 0);
        own(worth, SquareType::PennsylvaniaRailroad, 0);
        own(worth, SquareType::ElectricCompany, 0);
        worth.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].houses = 2;

        WorthFactors factors{};
        factors.property[static_cast<std::size_t>(rules::board::definition(SquareType::MediterraneanAvenue).group)] = 1.5;
        factors.cashCow[1] = 2.0;
        factors.cashCow[4] = 3.0;

        const auto& mediterranean = rules::board::definition(SquareType::MediterraneanAvenue);
        const auto& reading = rules::board::definition(SquareType::ReadingRailroad);
        const auto& pennsylvania = rules::board::definition(SquareType::PennsylvaniaRailroad);
        const auto& electric = rules::board::definition(SquareType::ElectricCompany);
        const auto expectedWorth =
            static_cast<std::int64_t>(mediterranean.housePurchaseCost) * 2 +
            static_cast<std::int64_t>(static_cast<double>(mediterranean.purchaseCost) * 1.5) +
            static_cast<std::int64_t>(static_cast<double>(reading.purchaseCost) * 2.0) +
            static_cast<std::int64_t>(static_cast<double>(pennsylvania.purchaseCost) * 2.0) +
            static_cast<std::int64_t>(static_cast<double>(electric.purchaseCost) * 3.0) + 123;
        require(ai::decision::totalWorthWithFactors(worth, 0, factors) == expectedWorth,
            "AI factored worth includes buildings cash and count-specific cash-cow factors");
        require(ai::decision::totalWorthWithFactors(worth, 2, factors) == 0,
            "AI factored worth rejects invalid player safely");

        auto roi = baseState();
        roi.players[0].cash = 10;
        own(roi, SquareType::OrientalAvenue, 0);
        own(roi, SquareType::BalticAvenue, 0);
        require(ai::decision::mortgageWorstProperty(roi, 0, false, false),
            "AI mortgage worst property finds ordinary mortgage candidate");
        require(roi.squares[static_cast<std::size_t>(SquareType::OrientalAvenue)].mortgaged &&
                !roi.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].mortgaged &&
                roi.players[0].cash == 10 + rules::board::definition(SquareType::OrientalAvenue).mortgageCost,
            "AI mortgage worst property preserves retail ROI order and credits mortgage cash");

        auto cashCow = baseState();
        own(cashCow, SquareType::ReadingRailroad, 0);
        own(cashCow, SquareType::PennsylvaniaRailroad, 0);
        own(cashCow, SquareType::ElectricCompany, 0);
        require(ai::decision::mortgageWorstProperty(cashCow, 0, false, false),
            "AI mortgage worst property can sacrifice lone utility before two-railroad cash cow");
        require(cashCow.squares[static_cast<std::size_t>(SquareType::ElectricCompany)].mortgaged &&
                !cashCow.squares[static_cast<std::size_t>(SquareType::ReadingRailroad)].mortgaged &&
                !cashCow.squares[static_cast<std::size_t>(SquareType::PennsylvaniaRailroad)].mortgaged,
            "AI mortgage worst property preserves minor-monopoly railroad utility exception");

        auto monopoly = baseState();
        own(monopoly, SquareType::MediterraneanAvenue, 0);
        own(monopoly, SquareType::BalticAvenue, 0);
        require(!ai::decision::mortgageWorstProperty(monopoly, 0, false, false),
            "AI mortgage worst property protects monopoly when monopoly mortgaging is disabled");
        require(ai::decision::mortgageWorstProperty(monopoly, 0, false, true),
            "AI mortgage worst property reaches undeveloped-monopoly fallback");
        const auto medMortgaged =
            monopoly.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].mortgaged;
        const auto balticMortgaged =
            monopoly.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].mortgaged;
        require(medMortgaged != balticMortgaged,
            "AI undeveloped-monopoly fallback fixes retail unsigned houses comparison bug");
        require(!ai::decision::mortgageWorstProperty(monopoly, 2, false, true),
            "AI mortgage worst property rejects invalid player safely");
    }

    void testWinningChances()
    {
        using ai::decision::CashStrategy;
        using ai::decision::WinningChanceConfig;

        auto unmortgage = baseState();
        unmortgage.players[0].cash = 500;
        own(unmortgage, SquareType::Boardwalk, 0);
        unmortgage.squares[static_cast<std::size_t>(SquareType::Boardwalk)].mortgaged = true;
        require(ai::decision::hypotheticalUnmortgageProperty(
                    unmortgage, 0, true, CashStrategy::MinimumAmount, 0) == SquareType::Go &&
                unmortgage.squares[static_cast<std::size_t>(SquareType::Boardwalk)].mortgaged,
            "AI generic hypothetical unmortgage honors monopoly-only mode");

        const auto unmortgageCost = static_cast<std::int64_t>(
            static_cast<double>(rules::board::definition(SquareType::Boardwalk).mortgageCost) * 1.1);
        require(ai::decision::hypotheticalUnmortgageProperty(
                    unmortgage, 0, false, CashStrategy::MinimumAmount, 0) == SquareType::Boardwalk,
            "AI generic hypothetical unmortgage selects highest-rent affordable property");
        require(!unmortgage.squares[static_cast<std::size_t>(SquareType::Boardwalk)].mortgaged &&
                unmortgage.players[0].cash == 500 - unmortgageCost,
            "AI generic hypothetical unmortgage debits selected property cost instead of stale retail pointer");

        auto equal = baseState();
        equal.players[0].cash = 200;
        equal.players[1].cash = 200;
        WinningChanceConfig config{};
        config.buyingStageCashMultiplier = 0.05;
        config.noMonopolyStageCashMultiplier = 0.025;
        config.cashLiquidAssetsDependence = 1.01;
        config.monopolyNotOwnedStageCashMultiplier = 0.01;
        config.monopolyOwnedStageCashMultiplier = 0.1;
        std::array<double, rules::MaxPlayers> chances{};
        require(ai::decision::evaluateWinningChances(
                    equal, rules::NobodyPlayer, config, chances) == 0.0,
            "AI winning-chance bulk mode preserves retail zero return contract");
        require(std::abs(chances[0] - 0.5) < 1e-12 &&
                std::abs(chances[1] - 0.5) < 1e-12,
            "AI winning chances normalize symmetric players to equal odds");

        auto richer = equal;
        richer.players[0].cash = 1000;
        richer.players[1].cash = 0;
        const auto richerChance = ai::decision::evaluateWinningChances(richer, 0, config);
        require(richerChance > 0.5 && richerChance < 1.0,
            "AI winning chances value liquid assets during buying stage");

        config.moneyOwed[0] = 1000;
        const auto debtChance = ai::decision::evaluateWinningChances(richer, 0, config);
        require(debtChance < richerChance && debtChance < 0.5,
            "AI winning chances include per-player debt while preserving retail tax asymmetry");
        require(ai::decision::evaluateWinningChances(equal, 2, config) == 0.0,
            "AI winning chances reject invalid single-player query safely");
    }

    void testMortgageNegativeCashPlayers()
    {
        auto state = baseState();
        state.numberOfPlayers = 3;
        state.players[0].cash = -20;
        state.players[1].cash = -20;
        state.players[2].cash = -20;
        state.players[2].currentSquare = static_cast<std::uint8_t>(SquareType::OffBoard);
        own(state, SquareType::OrientalAvenue, 0);
        own(state, SquareType::VermontAvenue, 1);

        ai::decision::mortgageNegativeCashPlayers(state);
        require(state.players[0].cash >= 0 &&
                state.squares[static_cast<std::size_t>(SquareType::OrientalAvenue)].mortgaged,
            "AI negative-cash resolver mortgages assets until active player is solvent");
        require(state.players[1].cash >= 0 &&
                state.squares[static_cast<std::size_t>(SquareType::VermontAvenue)].mortgaged,
            "AI negative-cash resolver processes every active player");
        require(state.players[2].cash == -20,
            "AI negative-cash resolver ignores off-board bankrupt players");
    }

    void testCounterProposalPreflight()
    {
        using ai::decision::CounterProposalPreflightConfig;
        using ai::decision::CounterProposalPreflightInputs;
        using ai::decision::CounterProposalSession;
        using ai::decision::CounterProposalStatus;
        using ai::trade::TradeProposalList;
        auto state = baseState();
        state.tradeInProgress = true;
        state.players[0].cash = 500;
        state.players[1].cash = 500;

        CounterProposalPreflightConfig config{};
        config.tradeCounterLimit = 3;
        config.numberTimesAllowPropertyTrade = 1;
        config.tradeCounterProbability = 1.0;
        config.evaluation.chancesThreshold = 1.0;
        config.evaluation.cashFactor = 1.0;
        config.playerAttitude[1] = 0.25;
        CounterProposalPreflightInputs inputs{};
        inputs.proposedPlayer = 1;

        TradeProposalList trade{};
        trade[0].cashGiven = 10;
        trade[1].cashReceived = 10;
        CounterProposalSession session{};
        auto result = ai::decision::counterProposalPreflight(
            state, 0, trade, inputs, config, session);
        require(result.status == CounterProposalStatus::Ready &&
                std::abs(result.evaluation + 10.0) < 1e-9 &&
                result.preparation.tradePlayerCount == 1 &&
                std::abs(result.averageAttitude - 0.25) < 1e-9,
            "AI counter preflight reaches ready state with retail preparation");
        auto purchaseConfig = config;
        purchaseConfig.evaluation.purchasingPlayer = 0;
        purchaseConfig.evaluation.purchasingProperty = SquareType::MediterraneanAvenue;
        auto purchaseSession = CounterProposalSession{};
        result = ai::decision::counterProposalPreflight(
            state, 0, trade, inputs, purchaseConfig, purchaseSession);
        require((result.preparation.playerProperties[0] &
                    rules::board::propertyBit(SquareType::MediterraneanAvenue)) != 0,
            "AI counter preflight includes pending purchase in retail property snapshot");


        auto invalidTime = inputs;
        invalidTime.pendingActions = 1;
        auto untouched = CounterProposalSession{};
        result = ai::decision::counterProposalPreflight(
            state, 0, trade, invalidTime, config, untouched);
        require(result.status == CounterProposalStatus::InvalidTime &&
                untouched.propertyMemory.bit1[1] == 0,
            "AI counter preflight rejects busy AI before remembering trade");

        TradeProposalList emptyTrade{};
        result = ai::decision::counterProposalPreflight(
            state, 0, emptyTrade, inputs, config, untouched);
        require(result.status == CounterProposalStatus::NotInvolved,
            "AI counter preflight preserves retail not-involved exit");

        std::array<ai::trade::FutureImmunityRecord, 1> immunity{{
            {0, 1, 0, 1, rules::TradeItemKind::Immunity}}};
        auto futureSession = CounterProposalSession{};
        result = ai::decision::counterProposalPreflight(
            state, 0, trade, inputs, config, futureSession, immunity);
        require(result.status == CounterProposalStatus::FutureOrImmunity &&
                futureSession.propertyMemory.bit1[1] == 0,
            "AI counter preflight rejects immunity before remembering trade");

        auto limitSession = CounterProposalSession{};
        limitSession.timesCounteredTrade = config.tradeCounterLimit;
        result = ai::decision::counterProposalPreflight(
            state, 0, trade, inputs, config, limitSession);
        require(result.status == CounterProposalStatus::TooManyCounters,
            "AI counter preflight rejects trade at retail counter limit");

        TradeProposalList propertyTrade{};
        const auto baltic = rules::board::propertyBit(SquareType::BalticAvenue);
        propertyTrade[0].propertiesGiven = baltic;
        propertyTrade[1].propertiesReceived = baltic;
        auto repeatedSession = CounterProposalSession{};
        ai::trade::rememberTradedProperties(
            repeatedSession.propertyMemory, 1, propertyTrade[0]);
        result = ai::decision::counterProposalPreflight(
            state, 0, propertyTrade, inputs, config, repeatedSession);
        require(result.status == CounterProposalStatus::RepeatedProperties,
            "AI counter preflight rejects repeated property offer before increment");

        auto probabilitySession = CounterProposalSession{};
        auto probabilityInputs = inputs;
        probabilityInputs.tradeAccept = true;
        probabilityInputs.counterRoll = 0.9;
        auto probabilityConfig = config;
        probabilityConfig.tradeCounterProbability = 0.5;
        result = ai::decision::counterProposalPreflight(
            state, 0, propertyTrade, probabilityInputs,
            probabilityConfig, probabilitySession);
        require(result.status == CounterProposalStatus::ProbabilitySkipped &&
                ai::trade::propertiesAlreadyTraded(
                    probabilitySession.propertyMemory, 1,
                    propertyTrade[0], 1) == baltic,
            "AI counter preflight remembers first property offer before probability skip");

        auto seriousSession = CounterProposalSession{};
        seriousSession.timesCounteredTrade = 1;
        seriousSession.lastTradeEvaluation = 0.0;
        result = ai::decision::counterProposalPreflight(
            state, 0, trade, inputs, config, seriousSession);
        require(result.status == CounterProposalStatus::NotSeriousTrader &&
                seriousSession.lastTradeEvaluation == 0.0,
            "AI counter preflight rejects non-improving counter without replacing saved evaluation");

        auto noPartnersTrade = TradeProposalList{};
        noPartnersTrade[0].cashGiven = 1;
        noPartnersTrade[0].cashReceived = 1;
        auto noPartnersSession = CounterProposalSession{};
        result = ai::decision::counterProposalPreflight(
            state, 0, noPartnersTrade, inputs, config, noPartnersSession);
        require(result.status == CounterProposalStatus::NoTradePartners,
            "AI counter preflight rejects malformed trade with no involved partner");

        auto annoyedConfig = config;
        annoyedConfig.playerAttitude[1] = -1.0;
        auto annoyedSession = CounterProposalSession{};
        result = ai::decision::counterProposalPreflight(
            state, 0, trade, inputs, annoyedConfig, annoyedSession);
        require(result.status == CounterProposalStatus::AnnoyedWithTrader,
            "AI counter preflight preserves retail annoyed single-partner exit");
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

    void testEvaluateTrade()
    {
        using ai::decision::TradeEvaluationConfig;
        using ai::trade::TradeProposalList;
        auto state = baseState();
        state.options.passingGoAmount = 200;
        state.players[0].cash = 500;
        state.players[1].cash = 500;

        TradeEvaluationConfig config{};
        config.chancesThreshold = 1.0;
        config.cashFactor = 1.0;

        TradeProposalList proposals{};
        proposals[0].cashGiven = 100;
        proposals[1].cashReceived = 100;
        require(std::abs(ai::decision::evaluateTrade(
                    state, 0, 0, proposals, config) + 100.0) < 1e-9,
            "AI trade evaluation applies weighted factored-worth cash delta");

        auto bankrupt = state;
        bankrupt.players[0].cash = 50;
        require(ai::decision::evaluateTrade(
                    bankrupt, 0, 0, proposals, config) == -50.0,
            "AI trade evaluation preserves retail bankruptcy sentinel");
        auto jail = state;
        jail.cards[static_cast<std::size_t>(rules::DeckType::Chance)].jailOwner = 0;
        TradeProposalList jailTrade{};
        jailTrade[1].jailCardReceived[static_cast<std::size_t>(rules::DeckType::Chance)] = true;
        require(std::abs(ai::decision::evaluateTrade(
                    jail, 0, 0, jailTrade, config) + 49.0) < 1e-9,
            "AI trade evaluation values transferred jail card at retail 49 dollars");

        auto monopoly = state;
        own(monopoly, SquareType::BalticAvenue, 1);
        TradeProposalList propertyTrade{};
        const auto baltic = rules::board::propertyBit(SquareType::BalticAvenue);
        propertyTrade[1].propertiesGiven = baltic;
        propertyTrade[0].propertiesReceived = baltic;
        TradeEvaluationConfig propertyConfig{};
        propertyConfig.chancesThreshold = 1.0;
        propertyConfig.tradeImportanceFactor = 1.0;
        propertyConfig.propertyImportance.propertyAllowTradeImportance = 10.0;
        propertyConfig.purchasingPlayer = 0;
        propertyConfig.purchasingProperty = SquareType::MediterraneanAvenue;
        require(ai::decision::evaluateTrade(
                    monopoly, 0, 0, propertyTrade, propertyConfig) > 0.0,
            "AI trade evaluation accounts for property currently being purchased");
        require(ai::decision::evaluateTrade(
                    state, 2, 0, {}, config) == -50.0,
            "AI trade evaluation rejects invalid evaluated player safely");
    }

    void testEvaluateTradePlayerList()
    {
        using ai::decision::TradeEvaluationConfig;
        using ai::trade::TradeProposalList;
        auto state = baseState();
        state.options.passingGoAmount = 200;
        state.players[0].cash = 500;
        state.players[1].cash = 500;

        TradeEvaluationConfig config{};
        config.chancesThreshold = 1.0;
        config.cashFactor = 1.0;
        TradeProposalList proposals{};
        proposals[0].cashGiven = 100;
        proposals[1].cashReceived = 100;
        const std::array<rules::PlayerNumber, 2> players{0, 1};
        std::array<double, 2> evaluations{};
        ai::decision::evaluateTradePlayerList(
            state, players, 0, proposals, false, config, evaluations);
        require(std::abs(evaluations[0] + 100.0) < 1e-9 &&
                std::abs(evaluations[1] - 100.0) < 1e-9,
            "AI trade player-list evaluation applies per-player worth deltas");
        require(std::abs(evaluations[0] - ai::decision::evaluateTrade(
                    state, 0, 0, proposals, config)) < 1e-9 &&
                std::abs(evaluations[1] - ai::decision::evaluateTrade(
                    state, 1, 0, proposals, config)) < 1e-9,
            "AI trade player-list bulk path matches single-player evaluation");

        auto bankrupt = state;
        bankrupt.players[0].cash = 50;
        evaluations = {};
        ai::decision::evaluateTradePlayerList(
            bankrupt, players, 0, proposals, false, config, evaluations);
        require(evaluations[0] == -50.0 && evaluations[1] == 100.0,
            "AI trade player-list isolates bankruptcy sentinel per player");

        const std::array<rules::PlayerNumber, 2> invalidPlayers{0, 3};
        evaluations = {};
        ai::decision::evaluateTradePlayerList(
            state, invalidPlayers, 0, proposals, false, config, evaluations);
        require(evaluations[1] == -50.0,
            "AI trade player-list rejects invalid participant safely");
    }

    void testMakeTradeFair()
    {
        using ai::decision::FairTradeConfig;
        using ai::trade::TradeProposalList;
        auto state = baseState();
        state.players[0].cash = 500;
        state.players[1].cash = 500;
        own(state, SquareType::MediterraneanAvenue, 0);
        const auto mediterranean = rules::board::propertyBit(SquareType::MediterraneanAvenue);
        const std::array<rules::PlayerNumber, 1> partners{1};

        FairTradeConfig config{};
        config.evaluation.chancesThreshold = 1.0;
        config.cashMultipliers.fill(1.0);
        TradeProposalList proposals{};
        proposals[0].propertiesGiven = mediterranean;
        proposals[1].propertiesReceived = mediterranean;
        require(ai::decision::makeTradeFair(
                    state, 0, partners, 16, false, proposals, config) &&
                proposals[0].cashReceived == 17 && proposals[1].cashGiven == 17,
            "AI make-trade-fair preserves retail halving sequence and final five-dollar step");
        TradeProposalList attitudeTrade{};
        attitudeTrade[0].propertiesGiven = mediterranean;
        attitudeTrade[1].propertiesReceived = mediterranean;
        auto attitudeConfig = config;
        attitudeConfig.cashMultipliers.fill(2.0);
        require(ai::decision::makeTradeFair(
                    state, 0, partners, 16, false, attitudeTrade, attitudeConfig) &&
                attitudeTrade[0].cashReceived == 34 && attitudeTrade[1].cashGiven == 34,
            "AI make-trade-fair scales requested cash by retail attitude multiplier");

        auto poor = state;
        poor.players[1].cash = 0;
        TradeProposalList poorTrade{};
        poorTrade[0].propertiesGiven = mediterranean;
        poorTrade[1].propertiesReceived = mediterranean;
        require(!ai::decision::makeTradeFair(
                    poor, 0, partners, 16, false, poorTrade, config),
            "AI make-trade-fair rejects partner unable to fund counter proposal");
        auto monopolySale = state;
        monopolySale.players[1].cash = 20;
        TradeProposalList monopolyTrade{};
        monopolyTrade[0].propertiesGiven = mediterranean;
        monopolyTrade[1].propertiesReceived = mediterranean;
        auto monopolyConfig = config;
        monopolyConfig.cashMultipliers.fill(2.0);
        require(ai::decision::makeTradeFair(
                    monopolySale, 0, partners, 16, true, monopolyTrade, monopolyConfig) &&
                monopolyTrade[0].cashReceived == 1 &&
                monopolyTrade[0].cashGiven == -17 &&
                monopolyTrade[1].cashGiven == 18,
            "AI make-trade-fair preserves retail monopoly-sale break asymmetry");

        TradeProposalList invalid{};
        invalid[0].propertiesGiven = mediterranean;
        invalid[1].propertiesReceived = mediterranean;
        require(!ai::decision::makeTradeFair(
                    state, 0, {}, 16, false, invalid, config),
            "AI make-trade-fair rejects empty partner list safely");
    }

    ai::decision::MonopolyProposalConfig monopolyProposalConfig()
    {
        ai::decision::MonopolyProposalConfig config{};
        config.fairTrade.evaluation.chancesThreshold = 1000000.0;
        config.fairTrade.evaluation.cashFactor = 1.0;
        config.fairTrade.evaluation.worthFactors.property.fill(1.0);
        config.fairTrade.minEvaluationThreshold = -1000000.0;
        config.fairTrade.minGiveMonopolyEvaluation = -1000000.0;
        config.fairTrade.cashMultipliers.fill(1.0);
        for (auto& generosity : config.whatToTrade)
            generosity.giveMonopoly = ai::trade::PropertyClassification::Best;
        return config;
    }

    void testMonopolyImportanceOrdering()
    {
        using ai::trade::MonopolySortOrder;
        using rules::board::SquareGroup;
        const auto descending = ai::trade::orderMonopolyImportance(
            0, MonopolySortOrder::Descending);
        require(descending.front() == SquareGroup::OrientalAvenue &&
                descending.back() == SquareGroup::MediterraneanAvenue,
            "AI monopoly order uses retail Darzinskis descending chart");

        const auto ascending = ai::trade::orderMonopolyImportance(
            0, MonopolySortOrder::Ascending);
        require(ascending.front() == SquareGroup::MediterraneanAvenue &&
                ascending.back() == SquareGroup::OrientalAvenue,
            "AI monopoly order uses retail Darzinskis ascending chart");

        const std::array<std::uint32_t, 8> randomKeys{70, 10, 60, 20, 50, 30, 40, 0};
        const auto random = ai::trade::orderMonopolyImportance(
            5000, MonopolySortOrder::Random, randomKeys);
        require(random[0] == SquareGroup::ParkPlace &&
                random[1] == SquareGroup::OrientalAvenue &&
                random[7] == SquareGroup::MediterraneanAvenue,
            "AI random monopoly order sorts injected retail rand keys ascending");
    }

    void testProactiveTradeOrchestration()
    {
        using ai::decision::ProactiveTradeInputs;
        using ai::decision::ProactiveTradeKind;
        using rules::board::SquareGroup;

        auto state = baseState();
        state.players[0].cash = state.players[1].cash = 100000;
        own(state, SquareType::MediterraneanAvenue, 0);
        own(state, SquareType::OrientalAvenue, 1);
        ai::trade::PropertySets properties{};
        properties[0] = ai::propertiesOwnedByPlayer(state, 0);
        properties[1] = ai::propertiesOwnedByPlayer(state, 1);
        std::array<double, rules::MaxPlayers> attitudes{};
        attitudes.fill(0.0);
        ProactiveTradeInputs inputs{};
        inputs.monopolyGroups = ai::trade::orderMonopolyImportance(
            ai::liquidAssets(state, 0, false, false),
            ai::trade::MonopolySortOrder::Descending);
        ai::decision::SemiImportantTradeInputs semiInputs{};
        semiInputs.wantedGroups.fill(SquareGroup::OrientalAvenue);
        semiInputs.offeredGroups.fill(SquareGroup::MediterraneanAvenue);
        semiInputs.partnerRoll = 0.0;
        semiInputs.wantedPropertyRoll = 0;
        semiInputs.offeredPropertyRoll = 1;
        inputs.semiImportant = semiInputs;
        const auto semi = ai::decision::buildProactiveTrade(
            state, 0, attitudes, properties, inputs, monopolyProposalConfig());
        require(semi.kind == ProactiveTradeKind::SemiImportant &&
                semi.proposal[0].propertiesReceived ==
                    rules::board::propertyBit(SquareType::OrientalAvenue),
            "AI proactive orchestrator falls through to retail semi-important trade");

        attitudes[1] = -1.1;
        const auto refused = ai::decision::buildProactiveTrade(
            state, 0, attitudes, properties, inputs, monopolyProposalConfig());
        require(refused.kind == ProactiveTradeKind::None,
            "AI proactive orchestrator excludes hostile partner before all trade branches");
        attitudes[1] = 0.0;
        inputs.importance = ai::trade::TradeDesperate;
        const auto desperate = ai::decision::buildProactiveTrade(
            state, 0, attitudes, properties, inputs, monopolyProposalConfig());
        require(desperate.kind == ProactiveTradeKind::SemiImportant,
            "AI desperate proactive trade bypasses retail attitude exclusion");

        auto giveawayState = baseState();
        giveawayState.players[0].cash = 100000;
        giveawayState.players[1].cash = 100000;
        own(giveawayState, SquareType::ParkPlace, 0);
        own(giveawayState, SquareType::Boardwalk, 0);
        own(giveawayState, SquareType::MediterraneanAvenue, 0);
        own(giveawayState, SquareType::BalticAvenue, 1);
        ai::trade::PropertySets giveawayProperties{};
        giveawayProperties[0] = ai::propertiesOwnedByPlayer(giveawayState, 0);
        giveawayProperties[1] = ai::propertiesOwnedByPlayer(giveawayState, 1);
        ProactiveTradeInputs giveawayInputs = inputs;
        giveawayInputs.importance = 0;
        giveawayInputs.shouldGiveAwayMonopoly = true;
        giveawayInputs.monopolyGroups = ai::trade::orderMonopolyImportance(
            ai::liquidAssets(giveawayState, 0, false, false),
            ai::trade::MonopolySortOrder::Descending);
        auto giveawayConfig = monopolyProposalConfig();
        const auto giveaway = ai::decision::buildProactiveTrade(
            giveawayState, 0, attitudes, giveawayProperties, giveawayInputs, giveawayConfig);
        require(giveaway.kind == ProactiveTradeKind::GiveMonopolyForCash &&
                (giveaway.proposal[0].propertiesGiven &
                 rules::board::propertyBit(SquareType::MediterraneanAvenue)) != 0,
            "AI proactive orchestrator prioritizes retail monopoly-for-cash branch");

        giveawayConfig.fairTrade.minGiveMonopolyEvaluation = 1000000.0;
        giveawayState.players[1].cash = 0;
        const auto failedGiveaway = ai::decision::buildProactiveTrade(
            giveawayState, 0, attitudes, giveawayProperties, giveawayInputs, giveawayConfig);
        require(failedGiveaway.kind == ProactiveTradeKind::None,
            "AI failed monopoly-for-cash target aborts instead of falling through like retail");
    }

    void testProactiveMonopolyBuilders()
    {
        auto state = baseState();
        state.players[0].cash = state.players[1].cash = 100000;
        own(state, SquareType::MediterraneanAvenue, 0);
        own(state, SquareType::BalticAvenue, 1);
        ai::trade::PropertySets properties{};
        properties[0] = ai::propertiesOwnedByPlayer(state, 0);
        properties[1] = ai::propertiesOwnedByPlayer(state, 1);
        auto config = monopolyProposalConfig();
        const std::array<rules::PlayerNumber, 1> partners{1};
        const auto group = rules::board::definition(SquareType::BalticAvenue).group;
        const auto mediterranean = rules::board::propertyBit(SquareType::MediterraneanAvenue);
        const auto baltic = rules::board::propertyBit(SquareType::BalticAvenue);
        ai::trade::TradeProposalList proposal{};
        require(ai::decision::buildMonopolyTrade(
                    state, 0, group, partners, properties, proposal, config) &&
                proposal[0].propertiesReceived == baltic &&
                proposal[1].propertiesGiven == baltic &&
                (proposal[0].propertiesGiven & mediterranean) == 0,
            "proactive monopoly acquisition requests missing lots without trading own lot to self");
        require(state.players[0].cash == 100000 &&
                state.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].owner == 1,
            "monopoly construction leaves real game state unchanged");

        const auto previous = proposal;
        auto invalid = state;
        invalid.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].owner = rules::BankPlayer;
        require(!ai::decision::buildMonopolyTrade(
                    invalid, 0, group, partners, properties, proposal, config) &&
                proposal == previous,
            "bank-owned target lot rejects safely without publishing partial proposal");
        require(!ai::decision::buildMonopolyTrade(
                    state, 0, group, {}, properties, proposal, config) && proposal == previous,
            "empty monopoly partner list leaves proposal unchanged");

        proposal = {};
        require(ai::decision::buildMonopolyForCash(
                    state, 0, 1, properties, proposal, config) &&
                proposal[0].propertiesGiven == mediterranean &&
                proposal[1].propertiesReceived == mediterranean &&
                ai::trade::tradeIsProper(state, proposal),
            "monopoly cash sale completes partner group and validates balanced proposal");
        const auto successfulSale = proposal;
        auto impossible = config;
        impossible.fairTrade.minGiveMonopolyEvaluation = 1000000.0;
        auto insolvent = state;
        insolvent.players[1].cash = 0;
        require(!ai::decision::buildMonopolyForCash(
                    insolvent, 0, 1, properties, proposal, impossible) &&
                proposal == successfulSale,
            "unaffordable monopoly cash sale preserves prior proposal");

        state.numberOfPlayers = 3;
        state.players[2].cash = 100000;
        state.players[2].currentSquare = static_cast<std::uint8_t>(SquareType::OffBoard);
        proposal = {};
        require(ai::decision::buildMonopolyForCash(
                    state, 0, 2, properties, proposal, config) &&
                proposal[1].propertiesReceived == mediterranean &&
                proposal[2] == ai::trade::TradeProposalRecord{},
            "cash sale wraps candidate order and skips bankrupt player and initiator");
        state.players[2].currentSquare = 0;
        own(state, SquareType::ParkPlace, 2);
        own(state, SquareType::Boardwalk, 2);
        properties[2] = ai::propertiesOwnedByPlayer(state, 2);
        proposal = {};
        require(ai::decision::buildMonopolyForCash(
                    state, 0, 2, properties, proposal, config) &&
                proposal[1].propertiesReceived == mediterranean &&
                proposal[2] == ai::trade::TradeProposalRecord{},
            "cash sale skips recipient already owning a monopoly");
        own(state, SquareType::MediterraneanAvenue, 1);
        own(state, SquareType::BalticAvenue, 0);
        own(state, SquareType::OrientalAvenue, 0);
        own(state, SquareType::VermontAvenue, 0);
        own(state, SquareType::ConnecticutAvenue, 0);
        properties[0] = ai::propertiesOwnedByPlayer(state, 0);
        properties[1] = ai::propertiesOwnedByPlayer(state, 1);
        proposal = {};
        require(ai::decision::buildMonopolyForCash(
                    state, 0, 1, properties, proposal, config) &&
                proposal[0].propertiesGiven == baltic,
            "cash sale excludes initiator's existing complete monopolies from offerings");
    }

    void testSemiImportantTradeBuilder()
    {
        auto state = baseState();
        state.players[0].cash = state.players[1].cash = 100000;
        own(state, SquareType::MediterraneanAvenue, 0);
        own(state, SquareType::OrientalAvenue, 1);
        ai::trade::PropertySets properties{};
        properties[0] = ai::propertiesOwnedByPlayer(state, 0);
        properties[1] = ai::propertiesOwnedByPlayer(state, 1);
        std::array<double, rules::MaxPlayers> attitudes{};
        attitudes.fill(0.0);
        ai::decision::SemiImportantTradeInputs inputs{};
        inputs.wantedGroups.fill(rules::board::SquareGroup::OrientalAvenue);
        inputs.offeredGroups.fill(rules::board::SquareGroup::MediterraneanAvenue);
        inputs.partnerRoll = 0.0;
        inputs.wantedPropertyRoll = 0;
        inputs.offeredPropertyRoll = 1;
        ai::trade::TradeProposalList proposal{};
        require(ai::decision::buildSemiImportantTrade(
                    state, 0, attitudes, properties, inputs, proposal,
                    monopolyProposalConfig()) &&
                proposal[0].propertiesReceived ==
                    rules::board::propertyBit(SquareType::OrientalAvenue) &&
                proposal[1].propertiesGiven ==
                    rules::board::propertyBit(SquareType::OrientalAvenue),
            "semi-important trade requests a property from the weighted retail partner");

        const auto previous = proposal;
        inputs.excludedPlayer = 1;
        require(!ai::decision::buildSemiImportantTrade(
                    state, 0, attitudes, properties, inputs, proposal,
                    monopolyProposalConfig()) && proposal == previous,
            "semi-important trade excludes the requested bad player transactionally");
    }

    void testCounterProposalBalance()
    {
        using ai::decision::CounterProposalBalanceConfig;
        using ai::decision::CounterProposalBalanceStatus;
        using ai::decision::CounterProposalPreflightResult;
        using ai::decision::CounterProposalStatus;
        using ai::trade::TradeProposalList;

        auto state = baseState();
        state.players[0].cash = 500;
        state.players[1].cash = 500;
        own(state, SquareType::MediterraneanAvenue, 0);
        const auto mediterranean = rules::board::propertyBit(SquareType::MediterraneanAvenue);

        CounterProposalPreflightResult preflight{};
        preflight.status = CounterProposalStatus::Ready;
        preflight.preparation.tradePlayerCount = 1;
        preflight.preparation.tradePlayers[0] = 1;
        preflight.preparation.playerProperties[0] = ai::propertiesOwnedByPlayer(state, 0);
        preflight.preparation.playerProperties[1] = ai::propertiesOwnedByPlayer(state, 1);
        CounterProposalBalanceConfig config{};
        config.fairTrade.evaluation.chancesThreshold = 1.0;
        config.fairTrade.cashMultipliers.fill(1.0);
        auto& importance = config.fairTrade.evaluation.propertyImportance;
        importance.monopolyReceivedImportance = 10.0;
        importance.propertyAllowTradeImportance = 10.0;
        importance.propertyTwoUnownedImportance = 10.0;
        importance.propertyOneUnownedImportance = 10.0;
        importance.railroadImportance = {10.0, 20.0, 30.0, 40.0};
        importance.utilityImportance = {10.0, 20.0};
        config.fairTrade.minEvaluationThreshold = 0.0;
        config.minEvaluationThreshold = 0.0;
        config.lowestPropertyImportanceForCounter = -100.0;
        config.maxGiveInTrade = 16;

        TradeProposalList proposals{};
        proposals[0].propertiesGiven = mediterranean;
        proposals[1].propertiesReceived = mediterranean;
        const auto result = ai::decision::counterProposalBalance(
            state, 0, preflight, proposals, config);
        require(result.status == CounterProposalBalanceStatus::Ready &&
                proposals[0].cashReceived == 17 &&
                proposals[1].cashGiven == 17 &&
                ai::trade::tradeIsProper(state, proposals),
            "AI counter balance normalizes and cash-balances a ready two-player trade");

        auto invalidPreflight = preflight;
        invalidPreflight.status = CounterProposalStatus::InvalidInput;
        auto invalidTrade = proposals;
        const auto invalidBefore = invalidTrade;
        const auto invalidResult = ai::decision::counterProposalBalance(
            state, 0, invalidPreflight, invalidTrade, config);
        require(invalidResult.status == CounterProposalBalanceStatus::InvalidInput &&
                invalidTrade == invalidBefore,
            "AI counter balance rejects non-ready preflight without mutation");

        auto poorState = baseState();
        poorState.players[0].cash = 500;
        poorState.players[1].cash = 500;
        CounterProposalPreflightResult poorPreflight{};
        poorPreflight.status = CounterProposalStatus::Ready;
        poorPreflight.preparation.tradePlayerCount = 1;
        poorPreflight.preparation.tradePlayers[0] = 1;
        TradeProposalList poorTrade{};
        poorTrade[0].cashGiven = 10;
        poorTrade[1].cashReceived = 10;
        auto poorConfig = config;
        poorConfig.minEvaluationThreshold = 1.0;
        poorConfig.lowestPropertyImportanceForCounter = 0.5;
        const auto poorResult = ai::decision::counterProposalBalance(
            poorState, 0, poorPreflight, poorTrade, poorConfig);
        require(poorResult.status == CounterProposalBalanceStatus::TooPoor &&
                poorResult.iterations == 1,
            "AI counter balance rejects too-poor trade after failing to add important property");
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
        testCashAvailableAfterHousing();
        testHypotheticalBuilding();
        testShouldUnmortgageAndBuyHouse();
        testEconomicActionPlanning();
        testTaxDecision();
        testJailExitChoice();
        testWorthFactorsAndMortgageWorstProperty();
        testWinningChances();
        testMortgageNegativeCashPlayers();
        testEvaluateTrade();
        testEvaluateTradePlayerList();
        testMakeTradeFair();
        testMonopolyImportanceOrdering();
        testProactiveMonopolyBuilders();
        testProactiveTradeOrchestration();
        testSemiImportantTradeBuilder();
        testCounterProposalBalance();
        testCounterProposalPreflight();
        testHypotheticalUnmortgageAndGiveAway();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
