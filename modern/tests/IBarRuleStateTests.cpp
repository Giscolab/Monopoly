#include "IBarRuleState.hpp"

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
                static_cast<std::uint8_t>(ibar::RuleMode::StatusScreen) == 26,
            "late IBAR_STATES numeric contract is preserved");
    }

    void testTurnAndDecisionModes()
    {
        ibar::RuleProjection projection;
        projection.process(notification(actions::Type::NotifyEndTurn, 2));
        require(projection.mode == ibar::RuleMode::DoneTurn &&
                projection.player == 2,
            "NOTIFY_END_TURN selects DoneTurn for its player");

        projection.process(notification(actions::Type::NotifyStartTurn, 3));
        require(projection.mode == ibar::RuleMode::Nothing &&
                projection.player == 3,
            "NOTIFY_START_TURN resets rules mode for the new player");

        projection.process(notification(actions::Type::NotifyPleaseRollDice, 3));
        require(projection.mode == ibar::RuleMode::StartTurn,
            "NOTIFY_PLEASE_ROLL_DICE selects StartTurn");

        projection.process(notification(actions::Type::NotifyBuyOrAuctionDecision, 3));
        require(projection.mode == ibar::RuleMode::BuyAuction,
            "NOTIFY_BUY_OR_AUCTION_DECISION selects BuyAuction");
