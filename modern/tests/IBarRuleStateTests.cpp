#include "IBarRuleState.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;

    void require(bool condition, const char* message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) throw std::runtime_error(message);
    }

    actions::Message notification(
        actions::Type action,
        std::int64_t player = 0)
    {
        actions::Message message{};
        message.action = action;
        message.numberA = player;
        return message;
    }

    void testNumericContract()
    {
        require(static_cast<std::uint8_t>(ibar::RuleMode::Nothing) == 0 &&
                static_cast<std::uint8_t>(ibar::RuleMode::StartTurn) == 1 &&
                static_cast<std::uint8_t>(ibar::RuleMode::DoneTurn) == 8,
            "early IBAR_STATES numeric contract is preserved");
        require(static_cast<std::uint8_t>(ibar::RuleMode::BuyAuction) == 11 &&
                static_cast<std::uint8_t>(ibar::RuleMode::JailExitPCR) == 13 &&
                static_cast<std::uint8_t>(ibar::RuleMode::Trading) == 17 &&
                static_cast<std::uint8_t>(ibar::RuleMode::GameOver) == 25 &&
                static_cast<std::uint8_t>(ibar::RuleMode::StatusScreen) == 26 &&
                static_cast<std::uint8_t>(ibar::RuleMode::Max) == 27,
            "late IBAR_STATES numeric contract is preserved");
    }

    void testTurnAndDecisionModes()
    {
        ibar::RuleProjection projection;
        projection.process(notification(actions::Type::NotifyEndTurn, 2));
        require(projection.mode == ibar::RuleMode::DoneTurn && projection.player == 2,
            "NOTIFY_END_TURN selects DoneTurn for its player");

        projection.process(notification(actions::Type::NotifyStartTurn, 3));
        require(projection.mode == ibar::RuleMode::Nothing && projection.player == 3,
            "NOTIFY_START_TURN resets rules mode for the new player");

        projection.process(notification(actions::Type::NotifyPleaseRollDice, 3));
        require(projection.mode == ibar::RuleMode::StartTurn && projection.player == 3,
            "NOTIFY_PLEASE_ROLL_DICE selects StartTurn");

        auto rolled = notification(actions::Type::NotifyDiceRolled, 6);
        rolled.numberB = 4;
        projection.process(rolled);
        require(projection.mode == ibar::RuleMode::Nothing && projection.player == 3,
            "NOTIFY_DICE_ROLLED clears mode without replacing current IBar player");

        projection.process(notification(actions::Type::NotifyPleasePay, 1));
        require(projection.mode == ibar::RuleMode::RaiseMoney && projection.player == 1,
            "NOTIFY_PLEASE_PAY selects RaiseMoney");

        projection.process(notification(actions::Type::NotifyBuyOrAuctionDecision, 1));
        require(projection.mode == ibar::RuleMode::BuyAuction && projection.player == 1,
            "NOTIFY_BUY_OR_AUCTION_DECISION selects BuyAuction");
    }

    void testJailModes()
    {
        struct JailCase
        {
            bool canRoll;
            bool hasCard;
            ibar::RuleMode expected;
        };

        constexpr std::array cases{
            JailCase{true,  true,  ibar::RuleMode::JailExitPCR},
            JailCase{true,  false, ibar::RuleMode::JailExitPXR},
            JailCase{false, true,  ibar::RuleMode::JailExitPCX},
            JailCase{false, false, ibar::RuleMode::JailExitPXX}
        };

        ibar::RuleProjection projection;
        for (const auto& test : cases)
        {
            auto message = notification(actions::Type::NotifyJailExitChoice, 4);
            message.numberB = test.canRoll ? 1 : 0;
            message.numberD = test.hasCard ? 1 : 0;
            projection.process(message);
            require(projection.mode == test.expected && projection.player == 4,
                "NOTIFY_JAIL_EXIT_CHOICE preserves all four legacy jail modes");
        }
    }

    void testCardsMortgageAndTax()
    {
        ibar::RuleProjection projection;
        projection.process(notification(actions::Type::NotifyPickedUpCard, 2));
        require(projection.mode == ibar::RuleMode::ViewingCard && projection.player == 2,
            "NOTIFY_PICKED_UP_CARD selects ViewingCard");

        projection.process(notification(actions::Type::NotifyPutAwayCard, 2));
        require(projection.mode == ibar::RuleMode::Nothing && projection.player == 2,
            "NOTIFY_PUT_AWAY_CARD returns to Nothing");

        projection.process(notification(actions::Type::NotifyEndTurn, 2));
        auto noFree = notification(actions::Type::NotifyFreeUnmortgaging, 2);
        noFree.numberB = 0;
        projection.process(noFree);
        require(projection.mode == ibar::RuleMode::DoneTurn,
            "empty NOTIFY_FREE_UNMORTGAGING leaves the current rules mode untouched");

        auto free = notification(actions::Type::NotifyFreeUnmortgaging, 2);
        free.numberB = 0x20;
        projection.process(free);
        require(projection.mode == ibar::RuleMode::FreeUnmortgage && projection.player == 2,
            "non-empty NOTIFY_FREE_UNMORTGAGING selects FreeUnmortgage");

        projection.process(notification(
            actions::Type::NotifyFlatOrFractionTaxDecision, 2));
        require(projection.mode == ibar::RuleMode::TaxDecision && projection.player == 2,
            "NOTIFY_FLAT_OR_FRACTION_TAX_DECISION selects TaxDecision");
    }

    void testBuildingAndGameOverModes()
    {
        ibar::RuleProjection projection;
        auto house = notification(actions::Type::NotifyPlaceBuilding, 1);
        house.numberB = -1;
        projection.process(house);
        require(projection.mode == ibar::RuleMode::PlaceHouse && projection.player == 1,
            "negative NOTIFY_PLACE_BUILDING selects PlaceHouse");

        auto hotel = notification(actions::Type::NotifyPlaceBuilding, 1);
        hotel.numberB = 1;
        projection.process(hotel);
        require(projection.mode == ibar::RuleMode::PlaceHotel && projection.player == 1,
            "non-negative NOTIFY_PLACE_BUILDING selects PlaceHotel");

        projection.process(notification(actions::Type::NotifyDecomposeSale, 1));
        require(projection.mode == ibar::RuleMode::HotelDecomposition && projection.player == 1,
            "NOTIFY_DECOMPOSE_SALE selects HotelDecomposition");

        projection.process(notification(actions::Type::NotifyGameOver, 0));
        require(projection.mode == ibar::RuleMode::GameOver && projection.player == 0,
            "NOTIFY_GAME_OVER selects GameOver");
    }

    void testAcceptedActionsClearMode()
    {
        constexpr std::array clearActions{
            actions::Type::EndTurn,
            actions::Type::RollDice,
            actions::Type::ExitJailDecision,
            actions::Type::CardSeen,
            actions::Type::GoBankrupt,
            actions::Type::BuyOrAuctionDecision,
            actions::Type::FreeUnmortgageDone,
            actions::Type::TaxDecision,
            actions::Type::StartHousingAuction
        };

        ibar::RuleProjection projection;
        for (const auto action : clearActions)
        {
            projection.mode = ibar::RuleMode::DoneTurn;
            projection.player = 2;
            auto completed = notification(actions::Type::NotifyActionCompleted);
            completed.numberA = static_cast<std::int64_t>(action);
            completed.numberB = 1;
            projection.process(completed);
            require(projection.mode == ibar::RuleMode::Nothing && projection.player == 2,
                "accepted legacy IBar actions clear the rules mode and retain player");
        }

        projection.mode = ibar::RuleMode::TaxDecision;
        projection.player = 1;
        auto rejected = notification(actions::Type::NotifyActionCompleted);
        rejected.numberA = static_cast<std::int64_t>(actions::Type::TaxDecision);
        rejected.numberB = 0;
        projection.process(rejected);
        require(projection.mode == ibar::RuleMode::TaxDecision && projection.player == 1,
            "rejected action completion does not clear the IBar rules mode");

        auto unrelated = notification(actions::Type::NotifyActionCompleted);
        unrelated.numberA = static_cast<std::int64_t>(actions::Type::TradeAccept);
        unrelated.numberB = 1;
        projection.process(unrelated);
        require(projection.mode == ibar::RuleMode::TaxDecision,
            "accepted non-clearing action leaves the IBar rules mode untouched");
    }

    void testInvalidInputsAndReset()
    {
        ibar::RuleProjection projection;
        projection.mode = ibar::RuleMode::BuyAuction;
        projection.player = 2;

        projection.process(notification(actions::Type::NotifyPleasePay,
            static_cast<std::int64_t>(rules::NobodyPlayer) + 1));
        require(projection.mode == ibar::RuleMode::BuyAuction && projection.player == 2,
            "invalid notification player cannot mutate the rules projection");

        projection.process(notification(actions::Type::NotifyCashAmount, 0));
        require(projection.mode == ibar::RuleMode::BuyAuction && projection.player == 2,
            "unrelated notifications leave the IBar rules projection untouched");

        auto players = notification(actions::Type::NotifyNumberOfPlayers);
        players.numberA = 4;
        projection.process(players);
        require(projection.mode == ibar::RuleMode::BuyAuction && projection.player == 2,
            "non-zero player-count notification does not reset IBar rules state");

        players.numberA = 0;
        projection.process(players);
        require(projection.mode == ibar::RuleMode::Nothing &&
                projection.player == rules::NobodyPlayer,
            "zero player-count notification resets IBar rules state");

        projection.mode = ibar::RuleMode::GameOver;
        projection.player = 0;
        projection.reset();
        require(projection.mode == ibar::RuleMode::Nothing &&
                projection.player == rules::NobodyPlayer,
            "explicit reset restores the initial IBar rules projection");
    }
}

int main()
{
    try
    {
        testNumericContract();
        testTurnAndDecisionModes();
        testJailModes();
        testCardsMortgageAndTax();
        testBuildingAndGameOverModes();
        testAcceptedActionsClearMode();
        testInvalidInputsAndReset();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
