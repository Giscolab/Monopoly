#include "AIUtility.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;
    using rules::board::SquareGroup;
    using rules::board::SquareType;

    void require(bool condition, const char* description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << description << '\n';
        if (!condition) throw std::runtime_error(description);
    }

    void own(rules::GameState& state, SquareType square,
             rules::PlayerNumber player)
    {
        state.squares[static_cast<std::size_t>(square)].owner = player;
    }

    rules::board::PropertySet bits(
        std::initializer_list<SquareType> squares)
    {
        rules::board::PropertySet result{};
        for (const auto square : squares)
            result |= rules::board::propertyBit(square);
        return result;
    }

    void testGroupContracts()
    {
        require(ai::groupRange(SquareGroup::MediterraneanAvenue).begin == 1 &&
                ai::groupRange(SquareGroup::MediterraneanAvenue).endExclusive == 4,
            "AI Mediterranean group preserves source [1,4) range");
        require(ai::groupRange(SquareGroup::OrientalAvenue).begin == 6 &&
                ai::groupRange(SquareGroup::OrientalAvenue).endExclusive == 10,
            "AI Oriental group preserves source [6,10) range");
        require(ai::groupRange(SquareGroup::ParkPlace).begin == 37 &&
                ai::groupRange(SquareGroup::ParkPlace).endExclusive == 40,
            "AI Park Place group preserves source [37,40) range");
        require(ai::groupRange(SquareGroup::Railroad).begin == 5 &&
                ai::groupRange(SquareGroup::Railroad).endExclusive == 36,
            "AI railroad scan preserves sparse source [5,36) range");
        require(ai::groupRange(SquareGroup::Utility).begin == 12 &&
                ai::groupRange(SquareGroup::Utility).endExclusive == 29,
            "AI utility scan preserves sparse source [12,29) range");

        require(ai::monopolySet(SquareType::BalticAvenue) ==
                bits({SquareType::MediterraneanAvenue, SquareType::BalticAvenue}),
            "AI monopoly set contains exactly the two brown properties");
        require(ai::monopolySet(SquareType::ConnecticutAvenue) == bits({
                SquareType::OrientalAvenue, SquareType::VermontAvenue,
                SquareType::ConnecticutAvenue}),
            "AI monopoly set skips Chance while scanning Oriental group");
        require(ai::monopolySet(SquareType::ReadingRailroad) == bits({
                SquareType::ReadingRailroad, SquareType::PennsylvaniaRailroad,
                SquareType::BAndORailroad, SquareType::ShortLineRailroad}),
            "AI railroad set preserves all four sparse railroads");
        require(ai::propertyCount(bits({SquareType::Boardwalk,
                    SquareType::ParkPlace, SquareType::ReadingRailroad})) == 3,
            "AI property-set bit counter matches retail loop");
        require(ai::testForMonopoly(bits({SquareType::ParkPlace,
                    SquareType::Boardwalk}), SquareType::Boardwalk) &&
                !ai::testForMonopoly(bits({SquareType::Boardwalk}),
                    SquareType::Boardwalk),
            "AI monopoly bit test distinguishes complete and partial sets");

        const auto brown = ai::monopolyLots(SquareType::MediterraneanAvenue);
        require(brown.count == 2 && brown.squares[0] == SquareType::BalticAvenue,
            "AI monopoly lots put the highest base-rent brown property first");
        const auto lightBlue = ai::monopolyLots(SquareType::OrientalAvenue);
        require(lightBlue.count == 3 &&
                lightBlue.squares[0] == SquareType::ConnecticutAvenue,
            "AI monopoly lots put Connecticut first without full sorting");
    }

    void testOwnershipContracts()
    {
        rules::GameState state{};
        own(state, SquareType::MediterraneanAvenue, 0);
        own(state, SquareType::BalticAvenue, 0);
        require(ai::propertiesOwnedByPlayer(state, 0) == bits({
                SquareType::MediterraneanAvenue, SquareType::BalticAvenue}),
            "AI reconstructs player property bitset from GameState owners");
        require(ai::isMonopoly(state, SquareType::BalticAvenue),
            "AI detects a complete colour monopoly by common owner");
        own(state, SquareType::MediterraneanAvenue, 1);
        require(!ai::isMonopoly(state, SquareType::BalticAvenue),
            "AI rejects a split-owner colour group");
        own(state, SquareType::MediterraneanAvenue, rules::NobodyPlayer);
        require(!ai::isMonopoly(state, SquareType::MediterraneanAvenue),
            "AI rejects a monopoly when the sampled square is unowned");

        for (const auto square : {SquareType::ReadingRailroad,
                 SquareType::PennsylvaniaRailroad, SquareType::BAndORailroad,
                 SquareType::ShortLineRailroad})
            own(state, square, 2);
        require(ai::isMonopoly(state, SquareType::ReadingRailroad),
            "AI_Is_Monopoly preserves legacy all-railroads-same-owner quirk");
        require(!ai::ownsMonopoly(state, 2, SquareType::ReadingRailroad, false),
            "AI_Own_Monopoly still excludes railroad groups");

        own(state, SquareType::MediterraneanAvenue, 0);
        state.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].mortgaged = true;
        require(ai::ownsMonopoly(state, 0, SquareType::BalticAvenue, false) &&
                !ai::ownsMonopoly(state, 0, SquareType::BalticAvenue, true),
            "AI mortgage-count flag preserves retail monopoly semantics");
        require(ai::playerOwnsMonopoly(state, 0, false) &&
                !ai::playerOwnsMonopoly(state, 0, true),
            "AI preserves source count_baltic quirk where true skips brown group");
        require(ai::anyMonopoly(state),
            "AI detects that at least one player owns a monopoly");
        require(ai::ownsPropertyFromMonopoly(state, 0, SquareType::BalticAvenue),
            "AI detects ownership of any property from a monopoly group");
        require(ai::firstOwnerInMonopoly(state, SquareType::BalticAvenue) == 0,
            "AI returns first non-bank monopoly owner in source scan order");
    }

    void testHousingContracts()
    {
        rules::GameState state{};
        state.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].houses = 2;
        state.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].houses = 3;
        require(ai::housesOnMonopoly(state, SquareType::BalticAvenue) == 5,
            "AI sums houses across every property in the monopoly");
        require(ai::housesCanBuyOnMonopoly(state, SquareType::BalticAvenue) == 5,
            "AI remaining-house capacity uses housesPerHotel times lot count");
        require(ai::freeHouses(state) == 27,
            "AI free-house count subtracts non-hotel house pieces from bank supply");

        state.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].houses = 5;
        state.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].houses = 5;
        require(ai::freeHouses(state) == 32 && ai::freeHotels(state) == 10,
            "AI hotel squares consume hotel pieces but no free-house inventory");

        state.options.maximumHouses = 3;
        state.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].houses = 2;
        state.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].houses = 2;
        require(ai::freeHouses(state) == 0,
            "AI clamps phantom hotel-decomposition house overuse to zero");
        require(ai::housingShortage(state, 0),
            "AI housing shortage is inclusive at critical level");
    }

    void testRentContracts()
    {
        rules::GameState state{};
        own(state, SquareType::MediterraneanAvenue, 0);
        own(state, SquareType::BalticAvenue, 0);
        const auto brown = ai::propertiesOwnedByPlayer(state, 0);
        require(ai::rentIfSteppedOn(state, SquareType::BalticAvenue, brown) == 8,
            "AI doubles undeveloped colour-group rent for a complete monopoly");
        state.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].houses = 2;
        require(ai::rentIfSteppedOn(state, SquareType::BalticAvenue, brown) == 60,
            "AI uses exact developed rent row for monopoly houses");
        state.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].mortgaged = true;
        require(ai::rentIfSteppedOn(state, SquareType::BalticAvenue, brown) == 0,
            "AI charges zero rent on a mortgaged property");

        rules::GameState rail{};
        own(rail, SquareType::ReadingRailroad, 1);
        auto railSet = ai::propertiesOwnedByPlayer(rail, 1);
        require(ai::rentIfSteppedOn(rail, SquareType::ReadingRailroad, railSet) == 25,
            "AI one-railroad rent is 25");
        for (const auto square : {SquareType::PennsylvaniaRailroad,
                 SquareType::BAndORailroad, SquareType::ShortLineRailroad})
            own(rail, square, 1);
        railSet = ai::propertiesOwnedByPlayer(rail, 1);
        require(ai::numberRailroadsUtilitiesOwned(rail, 1, SquareGroup::Railroad,
                    false, railSet) == 4 &&
                ai::rentIfSteppedOn(rail, SquareType::ReadingRailroad, railSet) == 200,
            "AI four-railroad count and rent preserve retail table");
        rules::GameState utility{};
        own(utility, SquareType::ElectricCompany, 2);
        auto utilitySet = ai::propertiesOwnedByPlayer(utility, 2);
        require(ai::rentIfSteppedOn(utility, SquareType::ElectricCompany, utilitySet) == 28,
            "AI one-utility average rent uses source roll-seven value 28");
        own(utility, SquareType::WaterWorks, 2);
        utilitySet = ai::propertiesOwnedByPlayer(utility, 2);
        require(ai::rentIfSteppedOn(utility, SquareType::WaterWorks, utilitySet) == 70,
            "AI two-utility average rent uses source roll-seven value 70");
        utility.squares[static_cast<std::size_t>(SquareType::ElectricCompany)].mortgaged = true;
        require(ai::numberRailroadsUtilitiesOwned(utility, 2, SquareGroup::Utility,
                    true, utilitySet) == 1,
            "AI mortgage-count mode excludes mortgaged utilities from group count");
    }


    void testExposureContracts()
    {
        require(std::abs(ai::landingFrequency(SquareType::Go) - 1.14) < 1e-12 &&
                std::abs(ai::landingFrequency(SquareType::IllinoisAvenue) - 1.19) < 1e-12 &&
                ai::landingFrequency(SquareType::InJail) == 0.0,
            "AI landing-frequency table preserves retail constants");

        rules::GameState state{};
        state.numberOfPlayers = 3;
        state.players[1].currentSquare = static_cast<std::uint8_t>(SquareType::LuxuryTax);
        state.players[2].currentSquare = static_cast<std::uint8_t>(SquareType::InJail);
        require(ai::playerCloseToProperty(state, 0, SquareType::BalticAvenue, 2, 12),
            "AI proximity wraps from before GO to brown properties");
        require(!ai::playerCloseToProperty(state, 0, SquareType::Boardwalk, 2, 12),
            "AI proximity preserves one-direction retail distance window");
        require(ai::someoneCloseToMonopoly(state, 0,
                    ai::monopolyLots(SquareType::BalticAvenue)),
            "AI monopoly proximity checks every lot using retail 2..12 window");

        rules::GameState mortgage{};
        mortgage.squares[static_cast<std::size_t>(SquareType::ParkPlace)].mortgaged = true;
        mortgage.squares[static_cast<std::size_t>(SquareType::Boardwalk)].mortgaged = true;
        require(ai::costUnmortgageMonopoly(mortgage, SquareType::Boardwalk) == 412,
            "AI unmortgage cost truncates each retail 110-percent lot cost");
        require(ai::isCashCow(SquareType::ReadingRailroad) &&
                ai::isCashCow(SquareType::ElectricCompany) &&
                !ai::isCashCow(SquareType::Boardwalk),
            "AI cash-cow classification is railroad or utility only");
    }

    void testIncomeContracts()
    {
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.players[0].cash = 1500;
        own(state, SquareType::MediterraneanAvenue, 0);
        own(state, SquareType::BalticAvenue, 0);
        require(ai::potentialIncome(state, 0) == 12,
            "AI potential income preserves undeveloped monopoly double-rent rule");
        state.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].houses = 1;
        require(ai::potentialIncome(state, 0) == 258,
            "AI potential income uses hotel rent only for already-developed lots");
        state.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].houses = 0;

        own(state, SquareType::ParkPlace, 1);
        own(state, SquareType::Boardwalk, 1);
        require(ai::mostExpensivePotentialIncome(state) == 1,
            "AI potential-income ranking selects strict highest player");

        rules::GameState rent{};
        rent.numberOfPlayers = 2;
        rent.players[0].cash = 1500;
        own(rent, SquareType::ReadingRailroad, 1);
        const auto rail = ai::propertiesOwnedByPlayer(rent, 1);
        const double expectedRail = 25.0 * 1.14 / 7.0;
        require(std::abs(ai::averageRentReceived(rent, 1, 0, false, 1.0, rail) -
                    expectedRail) < 1e-12,
            "AI average rent received applies landing frequency then divides by seven");
        require(std::abs(ai::averageRentReceived(rent, 1, 0, false, 2.0, rail) -
                    expectedRail * 2.0) < 1e-12,
            "AI cash-cow multiplier applies to railroad average rent");

        require(ai::averageRentPaid(rent, 0, 0, true, 1.0) == 17,
            "AI average rent paid preserves retail income/luxury-tax arithmetic");
        require(ai::averageRentPaid(rent, 0, 5, true, 1.0) == 14,
            "AI average rent paid skips income tax after its board position");
        rent.players[0].currentSquare = static_cast<std::uint8_t>(SquareType::InJail);
        require(ai::minimumCalculatedExpenses(rent, 0) == 10,
            "AI minimum expenses treats jail as Just Visiting before calculation");
        rent.players[0].currentSquare = static_cast<std::uint8_t>(SquareType::OffBoard);
        require(ai::minimumCalculatedExpenses(rent, 0) == -1,
            "AI minimum expenses rejects off-board players like retail");
    }
    void testAssetContracts()
    {
        rules::GameState state{};
        state.players[0].cash = 500;
        own(state, SquareType::MediterraneanAvenue, 0);
        own(state, SquareType::BalticAvenue, 0);
        own(state, SquareType::ReadingRailroad, 0);
        const auto owned = ai::propertiesOwnedByPlayer(state, 0);
        require(ai::liquidAssetsForProperties(state, owned, false, false) == 100,
            "AI liquid assets protect undeveloped monopolies when requested");
        require(ai::liquidAssetsForProperties(state, owned, false, true) == 160,
            "AI liquid assets include monopoly mortgages when enabled");
        state.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].houses = 2;
        state.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].houses = 2;
        require(ai::liquidAssetsForProperties(state, owned, true, true) == 260,
            "AI liquid assets add half-price houses plus mortgages exactly like source");
        require(ai::totalWorth(state, 0) == 1020,
            "AI total worth uses purchase price plus full house cost plus cash");

        state.squares[static_cast<std::size_t>(SquareType::ReadingRailroad)].mortgaged = true;
        require(ai::liquidAssetsForProperties(state, owned, true, true) == 160,
            "AI liquid assets ignore already-mortgaged property value");
    }
}

int main()
{
    try
    {
        require(rules::board::initializeForOptions(rules::GameOptions{}),
            "AI utility fixture initializes retail board definitions");
        testGroupContracts();
        testOwnershipContracts();
        testHousingContracts();
        testRentContracts();
        testExposureContracts();
        testIncomeContracts();
        testAssetContracts();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
