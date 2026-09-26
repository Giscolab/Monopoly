#include "IBar.hpp"
#include "IBarCameraButtonPlayback.hpp"
#include "CardTypes.hpp"
#include "Display.hpp"
#include "LocalPlayers.hpp"
#include "Messaging.hpp"
#include "PlayerSelection.hpp"
#include "RuntimeState.hpp"
#include "UserInterface.hpp"
#include "UISound.hpp"
#include "Timers.hpp"

#include <SDL3/SDL_scancode.h>

#include <array>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace test_support
{
    monopoly::display::State displayState{};
    monopoly::rules::GameState ruleState{};
    monopoly::runtime::State runtimeState{};
    monopoly::optionsui::State optionsState{};
    std::uint64_t tick = 0;
    int clickSoundCount = 0;
    std::vector<monopoly::actions::Message> sent;
    bool acceptSend = true;
    std::optional<monopoly::rules::PlayerNumber> clickedPlayer;
    std::array<bool, monopoly::rules::MaxPlayers> localHuman{{true, true, true, true, true, true}};
    int cameraCycleCount = 0;
    std::int32_t lastCameraSquare = -1;
    bool lastCameraSequential = false;
    int backdropSetCount = 0;
    monopoly::display::Screen2D lastBackdrop = monopoly::display::Screen2D::Invalid;
    int tradeBeginCount = 0;
    monopoly::rules::PlayerNumber tradeBeginPlayer = monopoly::rules::NobodyPlayer;
    bool tradeBeginAccepted = true;
    int optionsBeginCount = 0;
    bool optionsBeginAccepted = true;
    std::vector<monopoly::udsound::PennybagsVoice> pennybags;
}

namespace monopoly::engine
{
    void playClickSound() noexcept { ++test_support::clickSoundCount; }
    void playPennybagsVoice(udsound::PennybagsVoice voice,
        udsound::TokenVoiceClipPolicy, bool) noexcept
    {
        test_support::pennybags.push_back(voice);
    }
}

namespace monopoly::display
{
    const State& stateReadOnly() { return test_support::displayState; }
    void cycleIBarCamera(std::int32_t currentSquare, bool sequential) noexcept
    {
        ++test_support::cameraCycleCount;
        test_support::lastCameraSquare = currentSquare;
        test_support::lastCameraSequential = sequential;
    }
    void setBackdrop(Screen2D screen)
    {
        ++test_support::backdropSetCount;
        test_support::lastBackdrop = screen;
        test_support::displayState.desired2DView = screen;
    }
}

namespace monopoly::ui::localplayers
{
    bool slotIsLocalPlayer(rules::PlayerNumber) { return true; }
    bool slotIsLocalHumanPlayer(rules::PlayerNumber player)
    {
        return player < rules::MaxPlayers && test_support::localHuman[player];
    }
    bool slotIsLocalAIPlayer(rules::PlayerNumber) { return false; }
    rules::PlayerNumber anyLocalPlayer(const rules::GameState& state)
    {
        return state.numberOfPlayers > 0 ? 0 : rules::NobodyPlayer;
    }
}

namespace monopoly::timers
{
    std::uint64_t tickCount() { return test_support::tick; }
}

namespace monopoly::runtime
{
    State& state() { return test_support::runtimeState; }
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
    optionsui::State& optionsState() noexcept
    {
        return test_support::optionsState;
    }
    const optionsui::State& optionsStateReadOnly() noexcept
    {
        return test_support::optionsState;
    }
    const rules::GameState& ruleStateReadOnly()
    {
        return test_support::ruleState;
    }

    bool beginTradeFromIBar(rules::PlayerNumber player) noexcept
    {
        ++test_support::tradeBeginCount;
        test_support::tradeBeginPlayer = player;
        return test_support::tradeBeginAccepted;
    }

    bool beginOptionsFromIBar() noexcept
    {
        ++test_support::optionsBeginCount;
        return test_support::optionsBeginAccepted;
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
        if (!test_support::acceptSend) return false;
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
        test_support::pennybags.clear();
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


    void testPlayerScoreMouseOverTracking()
    {
        test_support::displayState.desired2DView = display::Screen2D::Main;
        test_support::ruleState.numberOfPlayers = 1;
        ibar::show();
        const auto rect = ibar::stateReadOnly().players[0].rect;
        ibar::processLibraryMessage({uimsg::Type::MouseMoved,
            rect.left + 1, rect.top + 1});
        require(ibar::stateReadOnly().playerCurrentMouseOver == 0,
            "gameplay mousemove over visible score box tracks exact player index");

        ibar::processLibraryMessage({uimsg::Type::MouseMoved, 755, 560});
        require(ibar::stateReadOnly().playerLastMouseOver == 0 &&
                ibar::stateReadOnly().playerCurrentMouseOver ==
                    static_cast<int>(rules::BankPlayer),
            "moving from player score box to bank preserves legacy shared hover state");
    }


  void testPlayerBankSelectionTracking()
    {
        test_support::displayState.desired2DView = display::Screen2D::Main;
        test_support::ruleState.numberOfPlayers = 2;
        test_support::localHuman[0] = true;
        test_support::localHuman[1] = true;
        ibar::show();

        require(ibar::resolveRuleMode(ibar::RuleMode::DoneTurn, 0) ==
                    ibar::RuleMode::DoneTurn &&
                ibar::resolveRulePlayer(0) == 0,
            "IBar initially tracks projected RULE player");

        const auto player1Rect = ibar::stateReadOnly().players[1].rect;
        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown,
            player1Rect.left + 1, player1Rect.top + 1});
        require(ibar::stateReadOnly().localRuleModeActive &&
                ibar::stateReadOnly().localRuleMode == ibar::RuleMode::OtherPlayer &&
                ibar::stateReadOnly().localRulePlayer == 1 &&
                ibar::resolveRuleMode(ibar::RuleMode::DoneTurn, 0) == ibar::RuleMode::OtherPlayer &&
                ibar::resolveRulePlayer(0) == 1,
            "clicking another local human enters persistent OtherPlayer selection");

        require(ibar::resolveRuleMode(ibar::RuleMode::StartTurn, 0) == ibar::RuleMode::OtherPlayer &&
                ibar::resolveRulePlayer(0) == 1,
            "wandering player selection survives subsequent RULE state change");

        ibar::setRuleActionHitState(Layout::General,
            mask({Slot::General1, Slot::Main}), ibar::RuleMode::OtherPlayer, 1, false);
        click(Slot::General1, Layout::General);
        require(ibar::resolveRuleMode(ibar::RuleMode::StartTurn, 0) == ibar::RuleMode::Build &&
                ibar::resolveRulePlayer(0) == 1,
            "BSSM entered while wandering preserves selected player");
        ibar::setRuleActionHitState(Layout::General, mask({Slot::Main}),
            ibar::RuleMode::Build, 1, false);
        click(Slot::Main, Layout::General);
        require(ibar::resolveRuleMode(ibar::RuleMode::StartTurn, 0) == ibar::RuleMode::OtherPlayer &&
                ibar::resolveRulePlayer(0) == 1,
            "Done from BSSM returns to inspected local player before tracking");
        ibar::setRuleActionHitState(Layout::General, mask({Slot::Main}),
            ibar::RuleMode::OtherPlayer, 1, false);
        click(Slot::Main, Layout::General);
        require(!ibar::stateReadOnly().localRuleModeActive &&
                ibar::resolveRuleMode(ibar::RuleMode::StartTurn, 0) == ibar::RuleMode::StartTurn &&
                ibar::resolveRulePlayer(0) == 0,
            "Done from OtherPlayer restores tracking of latest RULE player");

        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown, 755, 560});
        require(ibar::stateReadOnly().localRuleMode == ibar::RuleMode::OtherPlayer &&
                ibar::resolveRulePlayer(0) == rules::BankPlayer,
            "clicking Bank enters local OtherPlayer mode for RULE_MAX_PLAYERS");
        ibar::setRuleActionHitState(Layout::General, mask({Slot::Main}),
            ibar::RuleMode::OtherPlayer, rules::BankPlayer, false);
        click(Slot::Main, Layout::General);
        require(!ibar::stateReadOnly().localRuleModeActive && ibar::resolveRulePlayer(0) == 0,
            "Bank OtherPlayer Done returns to RULE tracking");

        test_support::localHuman[1] = false;
        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown,
            player1Rect.left + 1, player1Rect.top + 1});
        require(ibar::stateReadOnly().localRuleMode == ibar::RuleMode::OtherPlayerRemote &&
                ibar::resolveRulePlayer(0) == 1,
            "clicking non-local player enters OtherPlayerRemote");
        ibar::setRuleActionHitState(Layout::General, mask({Slot::Main}),
            ibar::RuleMode::OtherPlayerRemote, 1, false);
        click(Slot::Main, Layout::General);
        require(!ibar::stateReadOnly().localRuleModeActive &&
                ibar::resolveRuleMode(ibar::RuleMode::StartTurn, 0) == ibar::RuleMode::StartTurn,
            "OtherPlayerRemote Done is local and restores tracking");
        test_support::localHuman[1] = true;
    }

    void testForcedRuleTrackingRestore()
    {
        using namespace monopoly;
        test_support::displayState.desired2DView = display::Screen2D::Main;
        test_support::ruleState.numberOfPlayers = 2;
        test_support::localHuman[0] = true;
        test_support::localHuman[1] = true;
        ibar::show();

        const auto player1Rect = ibar::stateReadOnly().players[1].rect;
        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown,
            player1Rect.left + 1, player1Rect.top + 1});
        require(ibar::stateReadOnly().localRuleModeActive &&
                ibar::resolveRulePlayer(0) == 1,
            "fixture enters wandering player selection before forced tracking reset");

        ibar::restoreRuleTracking();
        require(!ibar::stateReadOnly().localRuleModeActive &&
                ibar::resolveRuleMode(ibar::RuleMode::StartTurn, 0) == ibar::RuleMode::StartTurn &&
                ibar::resolveRulePlayer(0) == 0,
            "forced tracking reset returns IBar to latest RULE mode/player");
    }

    void testForcedPlayerInspection()
    {
        using namespace monopoly;
        test_support::displayState.desired2DView = display::Screen2D::Main;
        test_support::ruleState.numberOfPlayers = 2;
        test_support::localHuman[1] = true;

        ibar::inspectPlayer(1);
        require(ibar::stateReadOnly().localRuleModeActive &&
                ibar::stateReadOnly().localRuleMode == ibar::RuleMode::OtherPlayer &&
                ibar::resolveRulePlayer(0) == 1,
            "forced inspection uses OtherPlayer for a local human");

        test_support::localHuman[1] = false;
        ibar::inspectPlayer(1);
        require(ibar::stateReadOnly().localRuleModeActive &&
                ibar::stateReadOnly().localRuleMode == ibar::RuleMode::OtherPlayerRemote &&
                ibar::resolveRulePlayer(0) == 1,
            "forced inspection uses OtherPlayerRemote for a non-local player");

        ibar::inspectPlayer(rules::BankPlayer);
        require(ibar::stateReadOnly().localRuleMode == ibar::RuleMode::OtherPlayer &&
                ibar::resolveRulePlayer(0) == rules::BankPlayer,
            "forced inspection keeps the retail BankPlayer OtherPlayer mode");

        ibar::restoreRuleTracking();
        test_support::localHuman[1] = true;
    }

    void testBankMouseOverTracking()
    {
        test_support::displayState.desired2DView = display::Screen2D::Main;
        ibar::processLibraryMessage({uimsg::Type::MouseMoved, 755, 560});
        require(ibar::stateReadOnly().playerCurrentMouseOver ==
                    static_cast<int>(rules::BankPlayer),
            "mousemove over [755,800)x[560,592) tracks legacy BankPlayer hover");

        ibar::show();
        require(ibar::stateReadOnly().playerCurrentMouseOver ==
                    static_cast<int>(rules::BankPlayer),
            "IBar show preserves bank hover while a gameplay IBar view is visible");

        ibar::processLibraryMessage({uimsg::Type::MouseMoved, 754, 559});
        require(ibar::stateReadOnly().playerLastMouseOver ==
                    static_cast<int>(rules::BankPlayer) &&
                ibar::stateReadOnly().playerCurrentMouseOver == -1,
            "moving outside bank rectangle records BankPlayer as last hover and clears current");
    }


    void testPropertyMouseOverTracking()
    {
        ibar::setPropertyHitState(ibar::layout::propertyBit(1));
        const auto rect = ibar::layout::propertyRect(1);
        ibar::processLibraryMessage({
            uimsg::Type::MouseMoved,
            (rect.left + rect.right) / 2,
            (rect.top + rect.bottom) / 2});
        require(ibar::stateReadOnly().propertyCurrentMouseOver == 1,
            "mousemove over a visible title records the exact property for hover playback");

        ibar::processLibraryMessage({uimsg::Type::MouseMoved, 799, 300});
        require(ibar::stateReadOnly().propertyCurrentMouseOver == -1 &&
                ibar::stateReadOnly().propertyLastMouseOver == 1,
            "moving off property titles clears current hover and preserves previous property");
        ibar::setPropertyHitState(0);
    }

    void testMainTurnAndCardActions()
    {
        struct MainAction
        {
            ibar::RuleMode mode;
            actions::Type action;
            std::uint8_t pressed;
        };
        const std::array cases{
            MainAction{ibar::RuleMode::StartTurn, actions::Type::RollDice, ibar::RollDiceButtonIndex},
            MainAction{ibar::RuleMode::DoneTurn, actions::Type::EndTurn, ibar::DoneButtonIndex},
            MainAction{ibar::RuleMode::ViewingCard, actions::Type::CardSeen, ibar::DoneButtonIndex},
            MainAction{ibar::RuleMode::FreeUnmortgage, actions::Type::FreeUnmortgageDone, ibar::DoneButtonIndex}
        };
        const auto activate = [](bool keyboard)
        {
            if (keyboard)
                ibar::processLibraryMessage({uimsg::Type::KeyboardPressed, SDL_SCANCODE_SPACE});
            else
                click(Slot::Main, Layout::General);
        };
        for (const auto& item : cases)
        {
            for (const bool keyboard : {false, true})
            {
                setHit(item.mode, Layout::General, mask({Slot::Main}));
                ibar::state().pendingPressedButton.reset();
                if (item.mode == ibar::RuleMode::ViewingCard)
                    ibar::state().desiredCardIndex = 3;
                activate(keyboard);
                expectSingle(item.action, 0, 0,
                    "Main click and Space send the exact RULE action for each turn/card phase");
                require(!ibar::stateReadOnly().pendingPressedButton &&
                        ibar::stateReadOnly().actionRuleMode == item.mode,
                    "input waits for RULE before pressed art or phase transition");
                if (item.mode == ibar::RuleMode::ViewingCard)
                    require(ibar::stateReadOnly().desiredCardIndex == 3,
                        "card remains displayed until RULE acknowledges CardSeen");
                actions::Message completed{};
                completed.action = actions::Type::NotifyActionCompleted;
                completed.fromPlayer = rules::BankPlayer;
                completed.toPlayer = rules::AllPlayers;
                completed.numberA = static_cast<std::int64_t>(item.action);
                completed.numberC = 0;
                ibar::processRuleMessage(completed, item.mode);
                require(!ibar::stateReadOnly().pendingPressedButton,
                    "rejected action does not animate an accepted button press");
                completed.numberB = 1;
                ibar::processRuleMessage(completed, item.mode);
                require(ibar::stateReadOnly().pendingPressedButton == item.pressed,
                    "real completion contract selects RollDice or Done pressed feedback");
                if (item.mode == ibar::RuleMode::ViewingCard)
                    require(!ibar::stateReadOnly().desiredCardIndex,
                        "accepted CardSeen clears the displayed card");
                ibar::clearPendingPressedButton(item.pressed);

                setHit(item.mode, Layout::General, 0);
                activate(keyboard);
                require(test_support::sent.empty(), "inactive Main mask blocks mouse and Space");
                setHit(item.mode, Layout::General, mask({Slot::Main}), true);
                activate(keyboard);
                require(test_support::sent.empty(), "remote Main blocks mouse and Space");
                setHit(item.mode, Layout::General, mask({Slot::Main}));
                ibar::setRuleActionHitState(Layout::General, mask({Slot::Main}),
                    item.mode, rules::BankPlayer, false);
                activate(keyboard);
                require(test_support::sent.empty(), "invalid action owner cannot send a Main action");
                setHit(item.mode, Layout::General, mask({Slot::Main}));
                ibar::setRuleActionHitState(Layout::General, mask({Slot::Main}),
                    item.mode, 1, false);
                activate(keyboard);
                require(test_support::sent.size() == 1 &&
                        test_support::sent[0].action == item.action &&
                        test_support::sent[0].fromPlayer == 1,
                    "Main action uses the resolved player instead of a fixed local slot");
                setHit(item.mode, Layout::General, mask({Slot::Main}));
                test_support::acceptSend = false;
                activate(keyboard);
                require(test_support::sent.empty() && !ibar::stateReadOnly().pendingPressedButton &&
                        ibar::stateReadOnly().actionRuleMode == item.mode,
                    "full queue leaves Main action available without fake acknowledgement");
                test_support::acceptSend = true;
                activate(keyboard);
                expectSingle(item.action, 0, 0, "Main input retries after queue capacity returns");
            }
        }
    }

    void testSpaceUsesCurrentMainLayout()
    {
        struct MainChoice
        {
            ibar::RuleMode mode;
            Layout layout;
            actions::Type action;
            std::int64_t choice;
        };
        const std::array cases{
            MainChoice{ibar::RuleMode::BuyAuction, Layout::BuyAuction, actions::Type::BuyOrAuctionDecision, 1},
            MainChoice{ibar::RuleMode::TaxDecision, Layout::TaxDecision, actions::Type::TaxDecision, 0},
            MainChoice{ibar::RuleMode::Trading, Layout::Trading, actions::Type::TradeAccept, 0}
        };
        for (const auto& item : cases)
        {
            setHit(item.mode, item.layout, mask({Slot::Main}));
            ibar::processLibraryMessage({uimsg::Type::KeyboardPressed, SDL_SCANCODE_SPACE});
            expectSingle(item.action, item.choice, 0,
                "Space selects the Main action in the active special layout");
        }
        setHit(ibar::RuleMode::StartTurn, Layout::General, mask({Slot::Main}));
        ibar::processLibraryMessage({uimsg::Type::KeyboardPressed, SDL_SCANCODE_RETURN});
        require(test_support::sent.empty(), "unmapped keys do not trigger Main");
        test_support::displayState.desired2DView = display::Screen2D::Options;
        ibar::processLibraryMessage({uimsg::Type::KeyboardPressed, SDL_SCANCODE_SPACE});
        require(test_support::sent.empty(), "Space cannot activate a hidden IBar");
        test_support::displayState.desired2DView = display::Screen2D::Main;
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

    void testBuyAuctionPopupNotificationState()
    {
        ibar::state().desiredBuyAuctionSquare.reset();
        actions::Message message{};
        message.action = actions::Type::NotifyBuyOrAuctionDecision;
        message.numberA = 0;
        message.numberB = 1;
        ibar::processRuleMessage(message, ibar::RuleMode::Nothing);
        require(ibar::stateReadOnly().desiredBuyAuctionSquare == 1,
            "NotifyBuyOrAuctionDecision records exact ownable square for popup deed");

        message = {};
        message.action = actions::Type::NotifyActionCompleted;
        message.numberA = static_cast<std::int64_t>(actions::Type::BuyOrAuctionDecision);
        message.numberB = 1;
        message.numberD = 1;
        ibar::processRuleMessage(message, ibar::RuleMode::BuyAuction);
        require(ibar::stateReadOnly().desiredBuyAuctionSquare == 1,
            "NotifyActionCompleted alone does not clear legacy Buy/Auction popup desired state");

        message = {};
        message.action = actions::Type::NotifyCashAmount;
        ibar::processRuleMessage(message, ibar::RuleMode::BuyAuction);
        require(!ibar::stateReadOnly().desiredBuyAuctionSquare,
            "routed UDIBar notification clears Buy/Auction popup before processing");

        message = {};
        message.action = actions::Type::NotifyBuyOrAuctionDecision;
        message.numberB = 3;
        ibar::processRuleMessage(message, ibar::RuleMode::Nothing);
        require(ibar::stateReadOnly().desiredBuyAuctionSquare == 3,
            "subsequent Buy/Auction notification replaces popup with new square");

        message = {};
        message.action = actions::Type::NotifyAuctionGoing;
        ibar::processRuleMessage(message, ibar::RuleMode::BuyAuction);
        require(!ibar::stateReadOnly().desiredBuyAuctionSquare,
            "auction-screen notification clears Buy/Auction popup like Userifce.cpp");

        message = {};
        message.action = actions::Type::NotifyBuyOrAuctionDecision;
        message.numberB = 0;
        ibar::processRuleMessage(message, ibar::RuleMode::Nothing);
        require(!ibar::stateReadOnly().desiredBuyAuctionSquare,
            "non-ownable square cannot create Buy/Auction popup state");
    }


    void testCashAnimationNotificationState()
    {
        auto& state = ibar::state();
        state.cashAnimationAmount.reset();
        state.cashAnimationTick = 0;
        state.buttonBarForceUpdate = false;

        actions::Message message{};
        message.action = actions::Type::NotifyCashAnimation;
        message.numberC = 275;
        ibar::processRuleMessage(message, ibar::RuleMode::Nothing, 42);
        require(state.cashAnimationAmount == 275 && state.cashAnimationTick == 42 && state.buttonBarForceUpdate,
            "NotifyCashAnimation publishes retail IBar cash message state");

        const auto amount = state.cashAnimationAmount;
        const auto tick = state.cashAnimationTick;
        message.numberC = 500;
        ibar::processRuleMessage(message, ibar::RuleMode::RaiseMoney);
        require(state.cashAnimationAmount == amount && state.cashAnimationTick == tick,
            "RaiseMoney suppresses cash animation message like retail IBar");
        state.buttonBarForceUpdate = false;
        message.numberC = 125;
        ibar::processRuleMessage(message, ibar::RuleMode::HotelDecomposition);
        require(state.cashAnimationAmount == amount && !state.buttonBarForceUpdate,
            "HotelDecomposition suppresses cash animation message like retail IBar");

        actions::Message decompose{};
        decompose.action = actions::Type::NotifyDecomposeSale;
        decompose.numberB = -4;
        ibar::processRuleMessage(decompose, ibar::RuleMode::Nothing, 99);
        require(!state.cashAnimationAmount &&
                state.decompositionHousesToSell == 4 &&
                state.buttonBarForceUpdate,
            "NotifyDecomposeSale publishes retail houses-to-sell button-bar state");

        state.buttonBarForceUpdate = false;
        message.numberC = 900;
        ibar::processRuleMessage(message, ibar::RuleMode::HotelDecomposition, 100);
        require(state.decompositionHousesToSell == 4 &&
                !state.buttonBarForceUpdate,
            "suppressed cash animation preserves decomposition message state");
    }

    void testCardNotificationState()
    {
        ibar::state().desiredCardIndex.reset();
        actions::Message message{};
        message.action = actions::Type::NotifyPickedUpCard;
        message.numberB = static_cast<std::int64_t>(rules::DeckType::Chance);
        message.numberC = static_cast<std::int64_t>(rules::CardType::ChanceGoDirectlyToGo);
        ibar::processRuleMessage(message, ibar::RuleMode::Nothing);
        require(ibar::stateReadOnly().desiredCardIndex == 0,
            "NotifyPickedUpCard maps first Chance card to display index 0");

        message.numberB = static_cast<std::int64_t>(rules::DeckType::Community);
        message.numberC = static_cast<std::int64_t>(rules::CardType::CommunityPay100ToBank);
        ibar::processRuleMessage(message, ibar::RuleMode::ViewingCard);
        require(ibar::stateReadOnly().desiredCardIndex == 31,
            "NotifyPickedUpCard maps final Community card to display index 31");

        message.action = actions::Type::NotifyPutAwayCard;
        ibar::processRuleMessage(message, ibar::RuleMode::ViewingCard);
        require(!ibar::stateReadOnly().desiredCardIndex,
            "NotifyPutAwayCard clears requested card visual");

        message.action = actions::Type::NotifyPickedUpCard;
        message.numberB = static_cast<std::int64_t>(rules::DeckType::Chance);
        message.numberC = static_cast<std::int64_t>(rules::CardType::ChanceGet50FromBank);
        ibar::processRuleMessage(message, ibar::RuleMode::ViewingCard);
        require(ibar::stateReadOnly().desiredCardIndex == 4,
            "second Chance pickup records exact zero-based display index");

        message = {};
        message.action = actions::Type::NotifyActionCompleted;
        message.numberA = static_cast<std::int64_t>(actions::Type::CardSeen);
        message.numberB = 1;
        ibar::processRuleMessage(message, ibar::RuleMode::ViewingCard);
        require(!ibar::stateReadOnly().desiredCardIndex &&
                ibar::stateReadOnly().pendingPressedButton == ibar::DoneButtonIndex,
            "accepted CardSeen clears card request and requests Done Pressed feedback");
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
                    ibar::RuleMode::Build &&
                test_support::pennybags == std::vector<udsound::PennybagsVoice>{
                    udsound::PennybagsVoice::PlayerClicks_BuyHouseHotel},
            "Build button enters local Build substate and plays retail host comment");

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
                    ibar::RuleMode::Sell &&
                test_support::pennybags == std::vector<udsound::PennybagsVoice>{
                    udsound::PennybagsVoice::PlayerClicks_SellHouseHotel},
            "Sell button enters local Sell substate and plays retail host comment");
        require(ibar::resolveRuleMode(ibar::RuleMode::StartTurn, 0) ==
                    ibar::RuleMode::Sell,
            "local BSSM override survives RULE mode change while IBar tracking is off");
        setHit(ibar::RuleMode::Sell, Layout::General, mask({Slot::Main}));
        click(Slot::Main, Layout::General);
        require(ibar::resolveRuleMode(ibar::RuleMode::StartTurn, 0) ==
                    ibar::RuleMode::StartTurn &&
                ibar::resolveRulePlayer(0) == 0,
            "Done returns tracked player to latest projected RULE state");

        setHit(ibar::RuleMode::StartTurn, Layout::General, mask({Slot::General3, Slot::Main}));
        click(Slot::General3, Layout::General);
        require(ibar::resolveRuleMode(ibar::RuleMode::StartTurn, 0) == ibar::RuleMode::Mortgage &&
                test_support::pennybags == std::vector<udsound::PennybagsVoice>{
                    udsound::PennybagsVoice::PlayerClicks_Mortgage},
            "Mortgage button enters local Mortgage substate and plays retail host comment");
        setHit(ibar::RuleMode::Mortgage, Layout::General, mask({Slot::Main}));
        click(Slot::Main, Layout::General);

        setHit(ibar::RuleMode::StartTurn, Layout::General, mask({Slot::General4, Slot::Main}));
        click(Slot::General4, Layout::General);
        require(ibar::resolveRuleMode(ibar::RuleMode::StartTurn, 0) == ibar::RuleMode::UnMortgage &&
                test_support::pennybags == std::vector<udsound::PennybagsVoice>{
                    udsound::PennybagsVoice::PlayerClicks_UnMortgage},
            "Unmortgage button enters local UnMortgage substate and plays retail host comment");
        setHit(ibar::RuleMode::UnMortgage, Layout::General, mask({Slot::Main}));
        click(Slot::Main, Layout::General);

        setHit(ibar::RuleMode::StartTurn, Layout::General, mask({Slot::Main}));
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


    void testGlobalCameraButton()
    {
        ibar::initialize();
        test_support::displayState = {};
        test_support::displayState.desired2DView = display::Screen2D::Main;
        test_support::ruleState = {};
        test_support::ruleState.numberOfPlayers = 2;
        test_support::ruleState.currentPlayer = 0;
        test_support::ruleState.players[0].currentSquare = 7;
        test_support::ruleState.players[1].currentSquare = 23;
        test_support::localHuman[0] = true;
        test_support::localHuman[1] = true;
        test_support::cameraCycleCount = 0;
        test_support::lastCameraSquare = -1;
        test_support::lastCameraSequential = false;

        (void)ibar::resolveRuleMode(ibar::RuleMode::StartTurn, 0);
        ibar::show();
        ibar::setRuleActionHitState(Layout::General, 0,
            ibar::RuleMode::StartTurn, 0, false);

        const auto cameraRect = ibar::layout::actionButtonRect(
            Slot::Camera, Layout::General);
        const int cameraX = (cameraRect.left + cameraRect.right) / 2;
        const int cameraY = (cameraRect.top + cameraRect.bottom) / 2;
        ibar::processLibraryMessage({uimsg::Type::MouseMoved, cameraX, cameraY});
        require(ibar::stateReadOnly().actionButtonCurrentMouseOver ==
                static_cast<int>(Slot::Camera),
            "Camera hover remains active independently of RULE action mask");

        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown, cameraX, cameraY});
        require(test_support::cameraCycleCount == 1 &&
                test_support::lastCameraSquare == 7 &&
                !test_support::lastCameraSequential &&
                ibar::stateReadOnly().pendingPressedButton == ibar::CameraButtonIndex,
            "Camera click uses tracked RULE player and requests Pressed feedback");
        ibar::clearPendingPressedButton(ibar::CameraButtonIndex);

        const auto player1Rect = ibar::stateReadOnly().players[1].rect;
        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown,
            player1Rect.left + 1, player1Rect.top + 1});
        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown, cameraX, cameraY});
        require(test_support::cameraCycleCount == 2 &&
                test_support::lastCameraSquare == 23,
            "Camera click follows locally inspected IBar player");

        test_support::displayState.mouseRightPressed = true;
        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown, cameraX, cameraY});
        require(test_support::cameraCycleCount == 3 &&
                test_support::lastCameraSequential,
            "right-button state selects sequential camera path");
        test_support::displayState.mouseRightPressed = false;

        uimsg::Message controlClick{uimsg::Type::MouseLeftDown, cameraX, cameraY};
        controlClick.numberE = uimsg::MouseModifierControl;
        ibar::processLibraryMessage(controlClick);
        require(test_support::cameraCycleCount == 4 &&
                test_support::lastCameraSequential,
            "Ctrl modifier selects same sequential camera path as right mouse");

        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown, 755, 560});
        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown, cameraX, cameraY});
        require(test_support::cameraCycleCount == 5 &&
                test_support::lastCameraSquare == 7,
            "Bank inspection safely falls back to projected RULE player camera square");

        test_support::displayState.desired2DView = display::Screen2D::Options;
        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown, cameraX, cameraY});
        require(test_support::cameraCycleCount == 5,
            "Camera physical hit is disabled when legacy IBar is not visible");
        test_support::displayState.desired2DView = display::Screen2D::Main;
    }

    void testGlobalStatusMainButton()
    {
        using namespace monopoly;
        test_support::runtimeState.gameInProgress = true;
        test_support::backdropSetCount = 0;
        test_support::lastBackdrop = display::Screen2D::Invalid;
        test_support::displayState.desired2DView = display::Screen2D::Main;
        ibar::setRuleActionHitState(Layout::General, 0,
            ibar::RuleMode::Nothing, 0, false);

        const auto rect = ibar::layout::actionButtonRect(Slot::Status, Layout::General);
        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown,
            (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2});
        require(test_support::backdropSetCount == 1 &&
                test_support::lastBackdrop == display::Screen2D::Portfolio &&
                ibar::stateReadOnly().pendingPressedButton == ibar::StatusButtonIndex,
            "Status global button enters Portfolio and requests Status pressed feedback");
        ibar::clearPendingPressedButton(ibar::StatusButtonIndex);

        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown,
            (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2});
        require(test_support::backdropSetCount == 2 &&
                test_support::lastBackdrop == display::Screen2D::Main &&
                ibar::stateReadOnly().pendingPressedButton == ibar::MainButtonIndex,
            "same retail hotspot returns Portfolio to Main with Main pressed feedback");
        ibar::clearPendingPressedButton(ibar::MainButtonIndex);

        test_support::displayState.desired2DView = display::Screen2D::Trade;
        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown,
            (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2});
        require(test_support::lastBackdrop == display::Screen2D::Main &&
                ibar::stateReadOnly().pendingPressedButton == ibar::MainButtonIndex,
            "Trade uses the same Main view button path as retail");
        ibar::clearPendingPressedButton(ibar::MainButtonIndex);

        test_support::runtimeState.gameInProgress = false;
        test_support::displayState.desired2DView = display::Screen2D::Main;
        const int before = test_support::backdropSetCount;
        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown,
            (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2});
        require(test_support::backdropSetCount == before,
            "Status/Main global hotspot is disabled outside an active game");
        test_support::runtimeState.gameInProgress = true;
    }

    void testGlobalOptionsButton()
    {
        test_support::displayState.desired2DView = display::Screen2D::Main;
        test_support::optionsBeginCount = 0;
        test_support::optionsBeginAccepted = true;
        setHit(ibar::RuleMode::StartTurn, Layout::General,
            mask({Slot::Options}));
        click(Slot::Options, Layout::General);
        require(test_support::optionsBeginCount == 1 &&
                ibar::stateReadOnly().pendingPressedButton == ibar::OptionsButtonIndex,
            "Options global button delegates to UDOpts flow and requests Pressed feedback");
        ibar::clearPendingPressedButton(ibar::OptionsButtonIndex);

        test_support::optionsBeginAccepted = false;
        click(Slot::Options, Layout::General);
        require(test_support::optionsBeginCount == 2 &&
                !ibar::stateReadOnly().pendingPressedButton,
            "Options button does not fake Pressed feedback when Options entry is rejected");
        test_support::optionsBeginAccepted = true;
    }

    void testGlobalTradeButton()
    {
        test_support::displayState.desired2DView = display::Screen2D::Main;
        test_support::ruleState.numberOfPlayers = 3;
        test_support::ruleState.currentPlayer = 0;
        test_support::ruleState.players[0].currentSquare = 7;
        test_support::ruleState.players[1].currentSquare = 23;
        test_support::tradeBeginCount = 0;
        test_support::tradeBeginPlayer = rules::NobodyPlayer;
        test_support::tradeBeginAccepted = true;

        setHit(ibar::RuleMode::StartTurn, Layout::General,
            mask({Slot::Trade}));
        click(Slot::Trade, Layout::General);
        require(test_support::tradeBeginCount == 1 &&
                test_support::tradeBeginPlayer == 0 &&
                ibar::stateReadOnly().pendingPressedButton == ibar::TradeButtonIndex,
            "Trade global button delegates to UDTrade flow and requests Pressed feedback");
        ibar::clearPendingPressedButton(ibar::TradeButtonIndex);

        test_support::tradeBeginAccepted = false;
        click(Slot::Trade, Layout::General);
        require(test_support::tradeBeginCount == 2 &&
                !ibar::stateReadOnly().pendingPressedButton,
            "Trade button does not fake Pressed feedback when Trade entry is rejected");

        test_support::tradeBeginAccepted = true;
        ibar::show();
        const auto player1Rect = ibar::stateReadOnly().players[1].rect;
        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown,
            player1Rect.left + 1, player1Rect.top + 1});
        click(Slot::Trade, Layout::General);
        require(test_support::tradeBeginCount == 3 &&
                test_support::tradeBeginPlayer == 1 &&
                ibar::stateReadOnly().pendingPressedButton == ibar::TradeButtonIndex,
            "Trade global button follows the locally inspected IBar player like the retail UI");
        ibar::clearPendingPressedButton(ibar::TradeButtonIndex);

        const auto player0Rect = ibar::stateReadOnly().players[0].rect;
        ibar::processLibraryMessage({uimsg::Type::MouseLeftDown,
            player0Rect.left + 1, player0Rect.top + 1});
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
        testPlayerScoreMouseOverTracking();
        testPlayerBankSelectionTracking();
        testForcedRuleTrackingRestore();
        testForcedPlayerInspection();
        testBankMouseOverTracking();
        testPropertyMouseOverTracking();
        testMainTurnAndCardActions();
        testSpaceUsesCurrentMainLayout();
        testBuyAuctionAndTax();
        testJailVariants();
        testTradeAndSpecialDirectActions();
        testPressedAcknowledgements();
        testBuyAuctionPopupNotificationState();
        testCashAnimationNotificationState();
        testCardNotificationState();
        testBSSMSubstatesAndDeeds();
        testGlobalCameraButton();
        testGlobalStatusMainButton();
        testGlobalOptionsButton();
        testGlobalTradeButton();
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
