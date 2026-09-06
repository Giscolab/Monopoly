#include "IBar.hpp"
#include "IBarCameraButtonPlayback.hpp"
#include "Display.hpp"
#include "LocalPlayers.hpp"
#include "Messaging.hpp"
#include "PlayerSelection.hpp"
#include "UserInterface.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace test_support
{
    monopoly::display::State displayState{};
    monopoly::rules::GameState ruleState{};
    std::vector<monopoly::actions::Message> sent;
    std::optional<monopoly::rules::PlayerNumber> clickedPlayer;
}

namespace monopoly::display
{
    const State& stateReadOnly() { return test_support::displayState; }
}

namespace monopoly::ui::localplayers
{
    bool slotIsLocalPlayer(rules::PlayerNumber) { return true; }
    bool slotIsLocalHumanPlayer(rules::PlayerNumber) { return true; }
    bool slotIsLocalAIPlayer(rules::PlayerNumber) { return false; }
}

namespace monopoly::playerselection
{
    void playerButtonClicked(rules::PlayerNumber player)
    {
        test_support::clickedPlayer = player;
    }
}

namespace monopoly::userinterface
{
    const rules::GameState& ruleStateReadOnly()
    {
        return test_support::ruleState;
    }
}

namespace monopoly::messaging
{
    bool sendAction(actions::Type action,
                    rules::PlayerNumber fromPlayer,
                    rules::PlayerNumber toPlayer,
                    std::int64_t numberA,
                    std::int64_t numberB,
                    std::int64_t numberC,
                    std::int64_t numberD,
                    std::wstring_view stringA)
    {
        actions::Message message{};
        message.action = action;
        message.fromPlayer = fromPlayer;
        message.toPlayer = toPlayer;
        message.numberA = numberA;
        message.numberB = numberB;
        message.numberC = numberC;
        message.numberD = numberD;
        std::copy_n(stringA.begin(),
            std::min(stringA.size(), message.stringA.size() - 1),
            message.stringA.begin());
        test_support::sent.push_back(std::move(message));
        return true;
    }
}

namespace
{
    using namespace monopoly;
    using Slot = ibar::layout::ActionButtonSlot;
    using Layout = ibar::layout::ActionButtonLayout;

    void require(bool condition, const char* description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << description << '\n';
        if (!condition) throw std::runtime_error(description);
    }

    ibar::layout::ActionButtonMask mask(
        std::initializer_list<Slot> slots)
    {
        ibar::layout::ActionButtonMask result = 0;
        for (const auto slot : slots)
            result |= ibar::layout::actionButtonBit(slot);
        return result;
    }

    void click(Slot slot, Layout layout)
    {
        const auto rect = ibar::layout::actionButtonRect(slot, layout);
        ibar::processLibraryMessage({
            uimsg::Type::MouseLeftDown,
            (rect.left + rect.right) / 2,
            (rect.top + rect.bottom) / 2});
    }

    void clickProperty(int square)
    {
        const auto rect = ibar::layout::propertyRect(square);
        ibar::processLibraryMessage({
            uimsg::Type::MouseLeftDown,
            (rect.left + rect.right) / 2,
            (rect.top + rect.bottom) / 2});
    }


    void setHit(ibar::RuleMode mode, Layout layout,
                ibar::layout::ActionButtonMask active,
                bool remote = false)
    {
        test_support::sent.clear();
        ibar::setRuleActionHitState(layout, active, mode, 0, remote);
    }

    void expectSingle(actions::Type action,
                      std::int64_t numberA,
                      std::int64_t numberB,
                      const char* description,
                      std::int64_t numberD = 0)
    {
        require(test_support::sent.size() == 1 &&
                test_support::sent[0].action == action &&
                test_support::sent[0].fromPlayer == 0 &&
                test_support::sent[0].toPlayer == rules::BankPlayer &&
                test_support::sent[0].numberA == numberA &&
                test_support::sent[0].numberB == numberB &&
                test_support::sent[0].numberD == numberD,
            description);
    }

    void testMaskedHitFiltering()
    {
        setHit(ibar::RuleMode::BuyAuction, Layout::BuyAuction,
            mask({Slot::Main}));
        click(Slot::General3, Layout::BuyAuction);
        require(test_support::sent.empty(),
            "masked-off physical button cannot send a RULE action");

        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown, 799, 599});
        require(test_support::sent.empty(),
            "click outside all active IBar rectangles is ignored");

        ibar::processLibraryMessage({uimsg::Type::MouseMoved, 300, 100});
        require(ibar::stateReadOnly().actionButtonCurrentMouseOver == -1,
            "mousemove outside active IBar buttons clears action hover");
    }

    void testBuyAuctionAndTax()
    {
        setHit(ibar::RuleMode::BuyAuction, Layout::BuyAuction,
            mask({Slot::Main, Slot::General3}));
        click(Slot::Main, Layout::BuyAuction);
        expectSingle(actions::Type::BuyOrAuctionDecision, 1, 0,
            "BuyAuction Main sends BUY decision");

        setHit(ibar::RuleMode::BuyAuction, Layout::BuyAuction,
            mask({Slot::Main, Slot::General3}));
        click(Slot::General3, Layout::BuyAuction);
        expectSingle(actions::Type::BuyOrAuctionDecision, 0, 0,
            "BuyAuction General3 sends AUCTION decision");

        setHit(ibar::RuleMode::TaxDecision, Layout::TaxDecision,
            mask({Slot::Main, Slot::General3}));
        click(Slot::Main, Layout::TaxDecision);
        expectSingle(actions::Type::TaxDecision, 0, 0,
            "TaxDecision Main sends flat-tax choice");

        setHit(ibar::RuleMode::TaxDecision, Layout::TaxDecision,
            mask({Slot::Main, Slot::General3}));
        click(Slot::General3, Layout::TaxDecision);
        expectSingle(actions::Type::TaxDecision, 1, 0,
            "TaxDecision General3 sends percentage choice");
    }

    void testJailVariants()
    {
        setHit(ibar::RuleMode::JailExitPCR, Layout::General,
            mask({Slot::Main, Slot::General2, Slot::General3}));
        click(Slot::Main, Layout::General);
        expectSingle(actions::Type::ExitJailDecision, 0, 0,
            "JailExitPCR Main sends roll choice");

        setHit(ibar::RuleMode::JailExitPXR, Layout::General,
            mask({Slot::Main, Slot::General2}));
        click(Slot::General2, Layout::General);
        expectSingle(actions::Type::ExitJailDecision, 1, 0,
            "JailExitPXR General2 sends pay choice");

        setHit(ibar::RuleMode::JailExitPCX, Layout::General,
            mask({Slot::General2, Slot::General3}));
        click(Slot::General3, Layout::General);
        expectSingle(actions::Type::ExitJailDecision, 2, 0,
            "JailExitPCX General3 sends card choice");

        setHit(ibar::RuleMode::JailExitPXX, Layout::General,
            mask({Slot::General2}));
        click(Slot::General2, Layout::General);
        expectSingle(actions::Type::ExitJailDecision, 1, 0,
            "JailExitPXX keeps pay as its only direct choice");
    }

    void testTradeAndSpecialDirectActions()
    {
        setHit(ibar::RuleMode::Trading, Layout::Trading,
            mask({Slot::Main, Slot::General2, Slot::General3}));
        click(Slot::Main, Layout::Trading);
        expectSingle(actions::Type::TradeAccept, 0, 0,
            "Trading Main sends reject");

        setHit(ibar::RuleMode::Trading, Layout::Trading,
            mask({Slot::Main, Slot::General2, Slot::General3}));
        click(Slot::General2, Layout::Trading);
        expectSingle(actions::Type::TradeAccept, 0, -1,
            "Trading General2 sends counter-offer");

        setHit(ibar::RuleMode::Trading, Layout::Trading,
            mask({Slot::Main, Slot::General2, Slot::General3}));
        click(Slot::General3, Layout::Trading);
        expectSingle(actions::Type::TradeAccept, 1, 1,
            "Trading General3 sends acceptance");

        setHit(ibar::RuleMode::RaiseMoney, Layout::General, mask({Slot::Main}));
        click(Slot::Main, Layout::General);
        expectSingle(actions::Type::GoBankrupt, 0, 0,
            "RaiseMoney Main sends bankruptcy request");

        setHit(ibar::RuleMode::HousingShort, Layout::General, mask({Slot::Main}));
        click(Slot::Main, Layout::General);
        expectSingle(actions::Type::StartHousingAuction, 0, 0,
            "HousingShort Main starts housing auction");

        setHit(ibar::RuleMode::HotelShort, Layout::General, mask({Slot::Main}));
        click(Slot::Main, Layout::General);
        expectSingle(actions::Type::StartHousingAuction, 0, 0,
            "HotelShort Main starts hotel shortage auction");
    }

    void testPressedAcknowledgements()
    {
        const auto acknowledge = [](actions::Type action,
                                    std::int64_t choice,
                                    ibar::RuleMode mode,
                                    bool accepted = true)
        {
            ibar::state().pendingPressedButton.reset();
            actions::Message message{};
            message.action = actions::Type::NotifyActionCompleted;
            message.numberA = static_cast<std::int64_t>(action);
            message.numberB = accepted ? 1 : 0;
            message.numberD = choice;
            ibar::processRuleMessage(message, mode);
            return ibar::stateReadOnly().pendingPressedButton;
        };

        require(!acknowledge(actions::Type::EndTurn, 0,
                    ibar::RuleMode::DoneTurn, false),
            "rejected action acknowledgement never requests Pressed art");
        require(acknowledge(actions::Type::EndTurn, 0,
                    ibar::RuleMode::DoneTurn) == ibar::DoneButtonIndex,
            "accepted EndTurn requests Done Pressed art");
        require(acknowledge(actions::Type::RollDice, 0,
                    ibar::RuleMode::StartTurn) == ibar::RollDiceButtonIndex,
            "accepted RollDice requests RollDice Pressed art");
        require(acknowledge(actions::Type::ExitJailDecision, 0,
                    ibar::RuleMode::JailExitPCR) == ibar::RollDiceButtonIndex &&
                acknowledge(actions::Type::ExitJailDecision, 1,
                    ibar::RuleMode::JailExitPCR) == ibar::PayButtonIndex &&
                acknowledge(actions::Type::ExitJailDecision, 2,
                    ibar::RuleMode::JailExitPCR) == ibar::UseCardButtonIndex,
            "jail acknowledgement maps roll/pay/card to exact pressed button");
        require(acknowledge(actions::Type::BuyOrAuctionDecision, 1,
                    ibar::RuleMode::BuyAuction) == ibar::BuyButtonIndex &&
                acknowledge(actions::Type::BuyOrAuctionDecision, 0,
                    ibar::RuleMode::BuyAuction) == ibar::AuctionButtonIndex,
            "buy/auction acknowledgement preserves original decision in Pressed art");
        require(acknowledge(actions::Type::TaxDecision, 0,
                    ibar::RuleMode::TaxDecision) == ibar::FlatTaxButtonIndex &&
                acknowledge(actions::Type::TaxDecision, 1,
                    ibar::RuleMode::TaxDecision) == ibar::PercentageButtonIndex,
            "tax acknowledgement maps flat/percentage to exact pressed button");
        require(acknowledge(actions::Type::GoBankrupt, 0,
                    ibar::RuleMode::RaiseMoney) == ibar::BankruptButtonIndex,
            "accepted bankruptcy requests Bankrupt Pressed art");
        require(acknowledge(actions::Type::StartHousingAuction, 0,
                    ibar::RuleMode::HousingShort) == ibar::AuctionHouseButtonIndex &&
                acknowledge(actions::Type::StartHousingAuction, 0,
                    ibar::RuleMode::HotelShort) == ibar::AuctionHotelButtonIndex,
            "shortage acknowledgement uses pre-clear RULE mode for house/hotel pressed art");
        require(!acknowledge(actions::Type::TradeAccept, 1,
                    ibar::RuleMode::Trading),
            "TradeAccept acknowledgement does not invent legacy Pressed feedback");
    }

    void testBSSMSubstatesAndDeeds()
    {
        test_support::ruleState.squares[1].owner = 0;
        test_support::ruleState.squares[3].owner = 1;

        require(ibar::resolveRuleMode(ibar::RuleMode::DoneTurn, 0) ==
                    ibar::RuleMode::DoneTurn,
            "RULE DoneTurn is the initial effective IBar mode");
        setHit(ibar::RuleMode::DoneTurn, Layout::General,
            mask({Slot::General1, Slot::General2, Slot::General3,
                  Slot::General4, Slot::Main}));
        click(Slot::General1, Layout::General);
        require(test_support::sent.empty() &&
                ibar::resolveRuleMode(ibar::RuleMode::DoneTurn, 0) ==
                    ibar::RuleMode::Build,
            "Build button enters local Build substate without mutating RULE");

        setHit(ibar::RuleMode::Build, Layout::General, mask({Slot::Main}));
        ibar::setPropertyHitState(ibar::layout::propertyBit(1));
        clickProperty(1);
        expectSingle(actions::Type::BuyHouse, 1, 0,
            "Build title sends quick BuyHouse for selected square", 1);

        setHit(ibar::RuleMode::Build, Layout::General, mask({Slot::Main}));
        click(Slot::Main, Layout::General);
        require(ibar::resolveRuleMode(ibar::RuleMode::DoneTurn, 0) ==
                    ibar::RuleMode::DoneTurn,
            "Done/Main exits local Build back to projected RULE state");

        setHit(ibar::RuleMode::DoneTurn, Layout::General,
            mask({Slot::General2, Slot::Main}));
        click(Slot::General2, Layout::General);
        require(ibar::resolveRuleMode(ibar::RuleMode::DoneTurn, 0) ==
                    ibar::RuleMode::Sell,
            "Sell button enters local Sell substate");
        require(ibar::resolveRuleMode(ibar::RuleMode::StartTurn, 0) ==
                    ibar::RuleMode::StartTurn,
            "RULE mode change cancels local BSSM override automatically");

        setHit(ibar::RuleMode::DoneTurn, Layout::General, mask({Slot::Main}));
        ibar::setPropertyHitState(
            ibar::layout::propertyBit(1) | ibar::layout::propertyBit(3));
        clickProperty(3);
        require(ibar::resolveRuleMode(ibar::RuleMode::DoneTurn, 0) ==
                    ibar::RuleMode::DoneTurn &&
                !ibar::stateReadOnly().selectedDeed,
            "clicking another player's title cannot enter DeedActive");

        setHit(ibar::RuleMode::DoneTurn, Layout::General, mask({Slot::Main}));
        ibar::setPropertyHitState(ibar::layout::propertyBit(1));
        clickProperty(1);
        require(ibar::resolveRuleMode(ibar::RuleMode::DoneTurn, 0) ==
                    ibar::RuleMode::DeedActive &&
                ibar::stateReadOnly().selectedDeed == 1,
            "owned title enters local DeedActive and records exact square");

        setHit(ibar::RuleMode::DeedActive, Layout::General,
            mask({Slot::General1, Slot::General2, Slot::General3,
                  Slot::General4, Slot::Main}));
        click(Slot::General3, Layout::General);
        expectSingle(actions::Type::Mortgaging, 1, 0,
            "DeedActive Mortgage sends quick action for recorded deed", 1);

        setHit(ibar::RuleMode::DeedActive, Layout::General, mask({Slot::Main}));
        click(Slot::Main, Layout::General);
        require(ibar::resolveRuleMode(ibar::RuleMode::DoneTurn, 0) ==
                    ibar::RuleMode::DoneTurn &&
                !ibar::stateReadOnly().selectedDeed,
            "DeedActive Done clears selected deed and resumes projected state");

        setHit(ibar::RuleMode::FreeUnmortgage, Layout::General, mask({Slot::Main}));
        ibar::setPropertyHitState(ibar::layout::propertyBit(1));
        clickProperty(1);
        expectSingle(actions::Type::Mortgaging, 1, 0,
            "FreeUnmortgage title sends fee-free non-quicky mortgage action", 0);

        setHit(ibar::RuleMode::Mortgage, Layout::General, mask({Slot::Main}));
        clickProperty(1);
        expectSingle(actions::Type::Mortgaging, 1, 0,
            "Mortgage substate title sends quick mortgage action", 1);

        setHit(ibar::RuleMode::PlaceHouse, Layout::General, mask({Slot::Main}));
        clickProperty(1);
        expectSingle(actions::Type::BuyHouse, 1, 0,
            "PlaceHouse title sends non-quicky placement action", 0);

        setHit(ibar::RuleMode::HotelDecomposition, Layout::General,
            mask({Slot::General2}));
        clickProperty(1);
        expectSingle(actions::Type::SellBuildings, 1, 0,
            "HotelDecomposition title sends quick SellBuildings", 1);
    }


    void testRemoteAndPlayerSelectGuards()
    {
        setHit(ibar::RuleMode::BuyAuction, Layout::BuyAuction,
            mask({Slot::Main, Slot::General3}), true);
        click(Slot::Main, Layout::BuyAuction);
        require(test_support::sent.empty(),
            "remote/AI grey action button cannot send local RULE action");

        test_support::displayState.desired2DView = display::Screen2D::PlayerSelect;
        test_support::ruleState.numberOfPlayers = 1;
        ibar::show();
        const auto rect = ibar::stateReadOnly().players[0].rect;
        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown,
            rect.left + 1, rect.top + 1});
        require(test_support::clickedPlayer == 0,
            "player-select click path remains intact after RULE-action wiring");
        test_support::displayState.desired2DView = display::Screen2D::Main;
    }
}

int main()
{
    try
    {
        test_support::displayState.desired2DView = monopoly::display::Screen2D::Main;
        test_support::ruleState.numberOfPlayers = 1;
        monopoly::ibar::initialize();
        testMaskedHitFiltering();
        testBuyAuctionAndTax();
        testJailVariants();
        testTradeAndSpecialDirectActions();
        testPressedAcknowledgements();
        testBSSMSubstatesAndDeeds();
        testRemoteAndPlayerSelectGuards();
        monopoly::ibar::shutdown();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
