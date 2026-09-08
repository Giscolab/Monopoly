#include "Actions.hpp"
#include "Display.hpp"
#include "RuntimeState.hpp"
#include "UserInterface.hpp"
#include "PieceCamera.hpp"
#include "RuleArchive.hpp"
#include "Messaging.hpp"

#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

namespace
{
    int failures = 0;
    bool acceptRecipient = true;
    int localResetCount = 0;
    int queueLockDepth = 0;
    monopoly::rules::PlayerNumber shortageResolvedPlayer = monopoly::rules::NobodyPlayer;
    monopoly::rules::PlayerNumber tradeResolvedPlayer = monopoly::rules::NobodyPlayer;
    monopoly::rules::PlayerNumber capturedTradeB = monopoly::rules::NobodyPlayer;
    std::uint32_t capturedTradePending = 0;
    std::uint64_t routingTick = 0;
    std::uint32_t localHumanMask = 0x3F;
    std::uint32_t localPlayerMask = 0x3F;
    std::size_t simulatedQueuedActions = 0;
    bool acceptMessaging = true;
    std::vector<monopoly::actions::Message> capturedMessages;
    std::optional<monopoly::auctionui::BidRequest> plannedAuctionBid;
    monopoly::actions::Message capturedAuctionAction{};
    bool capturedAuctionActionSent = false;
    monopoly::display::State routingDisplayState{};
    monopoly::display::Screen2D requestedBackdrop =
        monopoly::display::Screen2D::Invalid;
    std::vector<std::string_view> route;

    void expect(bool condition, std::string_view description)
    {
        if (condition)
        {
            std::cout << "[PASS] " << description << '\n';
            return;
        }

        ++failures;
        std::cerr << "[FAIL] " << description << '\n';
    }
}

namespace monopoly::display
{
    State& state()
    {
        return routingDisplayState;
    }

    void setBackdrop(Screen2D screen)
    {
        requestedBackdrop = screen;
        routingDisplayState.desired2DView = screen;
        route.push_back("display");
    }

    void noteBoardActivity() noexcept
    {
    }

    void setTokenAnimationStackActive(bool) noexcept
    {
    }

    void processBoardInput(const uimsg::Message&)
    {
        route.push_back("board");
    }
}

namespace monopoly::ui::localplayers
{
    bool isLocalRecipient(rules::PlayerNumber)
    {
        return acceptRecipient;
    }

    void reset()
    {
        ++localResetCount;
    }

    bool slotIsLocalPlayer(rules::PlayerNumber player)
    {
        return player < rules::MaxPlayers &&
            (localPlayerMask & (1u << player)) != 0;
    }

    bool slotIsLocalHumanPlayer(rules::PlayerNumber player)
    {
        return player < rules::MaxPlayers &&
            (localHumanMask & (1u << player)) != 0;
    }

    rules::PlayerNumber housingShortageIBarPlayer(
        const rules::GameState&,
        rules::PlayerNumber,
        std::uint32_t)
    {
        return shortageResolvedPlayer;
    }

    rules::PlayerNumber tradeAcceptanceIBarPlayer(
        const rules::GameState&,
        rules::PlayerNumber tradeBPlayer,
        std::uint32_t pendingPlayers)
    {
        capturedTradeB = tradeBPlayer;
        capturedTradePending = pendingPlayers;
        return tradeResolvedPlayer;
    }

    rules::PlayerNumber tradeSourcePlayer(
        const rules::GameState& state,
        rules::PlayerNumber current)
    {
        if (state.numberOfPlayers == 0) return rules::MaxPlayers;
        return current < state.numberOfPlayers ? current : rules::MaxPlayers;
    }

    void processRuleMessage(
        rules::GameState&,
        const actions::Message&)
    {
        route.push_back("localplayers");
    }
}

namespace monopoly::auctionui
{
    void reset(State&) noexcept
    {
    }

    RuleUpdate processRuleMessage(
        State&,
        const rules::GameState&,
        const actions::Message& message,
        display::Screen2D) noexcept
    {
        if (message.action == actions::Type::NotifyAuctionGoing ||
            message.action == actions::Type::NotifyNewHighBid ||
            (message.action == actions::Type::NotifyAreYouThere &&
             message.numberC == static_cast<std::int64_t>(
                 actions::Type::NotifyNewHighBid)))
            route.push_back("auction-rule");
        return {};
    }

    std::optional<BidRequest> planBid(
        const State&,
        rules::PlayerNumber,
        display::Screen2D,
        const uimsg::Message&,
        std::uint32_t) noexcept
    {
        route.push_back("auction-ui");
        return plannedAuctionBid;
    }
}

namespace monopoly::messaging
{
    bool sendAction(const actions::Message& message)
    {
        if (!acceptMessaging) return false;
        capturedAuctionAction = message;
        capturedAuctionActionSent = true;
        capturedMessages.push_back(message);
        ++simulatedQueuedActions;
        route.push_back("messaging");
        return true;
    }

    bool sendAction(
        actions::Type action,
        rules::PlayerNumber fromPlayer,
        rules::PlayerNumber toPlayer,
        std::int64_t numberA,
        std::int64_t numberB,
        std::int64_t numberC,
        std::int64_t numberD,
        std::wstring_view)
    {
        actions::Message message{};
        message.action = action;
        message.fromPlayer = fromPlayer;
        message.toPlayer = toPlayer;
        message.numberA = numberA;
        message.numberB = numberB;
        message.numberC = numberC;
        message.numberD = numberD;
        return sendAction(message);
    }

    std::size_t queuedActionCount()
    {
        return simulatedQueuedActions;
    }
}

namespace monopoly::playerselection
{
    void processMessage(const actions::Message&)
    {
        route.push_back("playerselection");
    }

    void processLibraryMessage(const uimsg::Message&)
    {
        route.push_back("playerselection-ui");
    }
}

namespace monopoly::ibar
{
    void processLibraryMessage(const uimsg::Message&)
    {
        route.push_back("ibar-ui");
    }

    void processRuleMessage(const actions::Message&, RuleMode) noexcept
    {
    }
}

namespace monopoly::timers
{
    std::uint64_t tickCount() { return routingTick; }
}

namespace monopoly::userinterface
{
    void advanceTimeStep()
    {
    }

    void lockGameQueue()
    {
        ++queueLockDepth;
    }

    void unlockGameQueue()
    {
        if (queueLockDepth > 0) --queueLockDepth;
    }

    bool gameQueueLocked()
    {
        return queueLockDepth > 0;
    }
}

namespace
{
    void testUiModuleOrder()
    {
        using namespace monopoly;
        route.clear();
        plannedAuctionBid.reset();
        runtime::reset();
        uimsg::Message message{};
        message.type = uimsg::Type::MouseMoved;
        message.numberA = 100;
        message.numberB = 100;
        expect(userinterface::processUIMessage(message),
            "ordinary UI message keeps game running");
        expect(route == std::vector<std::string_view>{
                "auction-ui", "board", "ibar-ui", "playerselection-ui"},
            "UI routing preserves UDAuct then UDBoard then UDIBar then PlayerSelection order");
    }

    void testAuctionBidRouting()
    {
        using namespace monopoly;
        route.clear();
        capturedAuctionActionSent = false;
        plannedAuctionBid = auctionui::BidRequest{1, 620, 2};
        routingDisplayState.desired2DView = display::Screen2D::Auction;

        uimsg::Message message{};
        message.type = uimsg::Type::MouseLeftDown;
        message.numberA = 250;
        message.numberB = 480;
        expect(userinterface::processUIMessage(message),
            "auction bill click keeps game running");
        expect(route == std::vector<std::string_view>{
                "auction-ui", "messaging", "board", "ibar-ui", "playerselection-ui"},
            "auction bid is dispatched before later historical UI modules");
        expect(capturedAuctionActionSent &&
            capturedAuctionAction.action == actions::Type::Bid &&
            capturedAuctionAction.fromPlayer == 1 &&
            capturedAuctionAction.toPlayer == rules::BankPlayer &&
            capturedAuctionAction.numberA == 620,
            "auction bid plan becomes ACTION_BID(player -> bank, absolute bid)");
        plannedAuctionBid.reset();
    }

    void testAuctionRuleRouting()
    {
        using namespace monopoly;
        route.clear();
        acceptRecipient = true;

        actions::Message message{};
        message.action = actions::Type::NotifyNewHighBid;
        message.toPlayer = rules::AllPlayers;
        userinterface::processRuleMessage(message);
        expect(route == std::vector<std::string_view>{
                "auction-rule", "localplayers", "playerselection"},
            "auction rule notification reaches UDAuct projection before generic local/player setup projections");
    }

    void testAuctionReadyResponses()
    {
        using namespace monopoly;
        route.clear();
        capturedMessages.clear();
        simulatedQueuedActions = 0;
        acceptMessaging = true;
        localPlayerMask = 0b0101;
        localHumanMask = 0b0001;
        userinterface::ruleState().numberOfPlayers = 4;

        const auto sent = userinterface::sendAuctionReadyResponses(0b1111, 77);
        expect(sent && capturedMessages.size() == 2 &&
                capturedMessages[0].action == actions::Type::IAmHere &&
                capturedMessages[0].fromPlayer == 0 &&
                capturedMessages[0].toPlayer == rules::BankPlayer &&
                capturedMessages[0].numberA == 77 &&
                capturedMessages[1].action == actions::Type::IAmHere &&
                capturedMessages[1].fromPlayer == 2 &&
                capturedMessages[1].toPlayer == rules::BankPlayer &&
                capturedMessages[1].numberA == 77,
            "auction Begin answers roll-call for every local player, including local AI");

        capturedMessages.clear();
        simulatedQueuedActions = messaging::MessageQueueCapacity - 1;
        const auto full = userinterface::sendAuctionReadyResponses(0b0101, 91);
        expect(!full && capturedMessages.empty() &&
                simulatedQueuedActions == messaging::MessageQueueCapacity - 1,
            "auction roll-call preflights FIFO capacity before sending any I_AM_HERE");

        localPlayerMask = 0x3F;
        localHumanMask = 0x3F;
        simulatedQueuedActions = 0;
        capturedMessages.clear();
    }

    void testTradeEntryAndPartnerRouting()
    {
        using namespace monopoly;

        userinterface::resetRuleProjection();
        runtime::reset();
        runtime::state().gameInProgress = true;
        auto& uiState = userinterface::ruleState();
        uiState.numberOfPlayers = 4;
        uiState.currentPlayer = 0;
        for (rules::PlayerNumber player = 0; player < uiState.numberOfPlayers; ++player)
            uiState.players[player].currentSquare = player;
        localHumanMask = 0x0Fu;
        localPlayerMask = 0x0Fu;
        routingDisplayState.desired2DView = display::Screen2D::Main;
        requestedBackdrop = display::Screen2D::Invalid;
        route.clear();

        expect(userinterface::beginTradeFromIBar(0),
            "IBar Trade entry accepts an active local source player");
        const auto& opening = userinterface::tradeStateReadOnly();
        expect(requestedBackdrop == display::Screen2D::Trade &&
                routingDisplayState.desired2DView == display::Screen2D::Trade &&
                opening.playerA == 0 && opening.playerB == rules::MaxPlayers &&
                opening.playerSelectVisible && opening.ignoreEntryClick,
            "IBar Trade entry switches backdrop and opens the retail partner selector");

        const auto partnerRect = tradeui::playerTokenRect(opening, uiState, 1);
        expect(partnerRect.has_value(),
            "eligible partner exposes the retail token hit rectangle");
        if (!partnerRect)
        {
            requestedBackdrop = display::Screen2D::Invalid;
            routingDisplayState.desired2DView = display::Screen2D::Main;
            localHumanMask = 0x3Fu;
            localPlayerMask = 0x3Fu;
            runtime::reset();
            return;
        }

        uimsg::Message click{};
        click.type = uimsg::Type::MouseLeftDown;
        click.numberA = (partnerRect->left + partnerRect->right) / 2;
        click.numberB = (partnerRect->top + partnerRect->bottom) / 2;
        expect(userinterface::processUIMessage(click) &&
                userinterface::tradeStateReadOnly().playerB == rules::MaxPlayers &&
                !userinterface::tradeStateReadOnly().ignoreEntryClick,
            "first Trade mouse-down is swallowed by the historical first-time guard");
        expect(userinterface::processUIMessage(click) &&
                userinterface::tradeStateReadOnly().playerB == 1 &&
                !userinterface::tradeStateReadOnly().playerSelectVisible &&
                userinterface::tradeStateReadOnly().desiredTradePanels == 1,
            "second Trade mouse-down selects B through the full UserInterface route");

        auto& storedTrade = userinterface::tradeState();
        actions::Message storedItem{};
        storedItem.action = actions::Type::TradeItem;
        storedTrade.items.push_back(storedItem);
        routingDisplayState.desired2DView = display::Screen2D::Main;
        requestedBackdrop = display::Screen2D::Invalid;
        expect(userinterface::beginTradeFromIBar(0) &&
                requestedBackdrop == display::Screen2D::Trade &&
                storedTrade.playerA == 0 && storedTrade.playerB == 1 &&
                storedTrade.items.size() == 1 && !storedTrade.playerSelectVisible &&
                storedTrade.ignoreEntryClick,
            "reopening Trade preserves a valid non-empty stored editor instead of clearing it");

        requestedBackdrop = display::Screen2D::Invalid;
        routingDisplayState.desired2DView = display::Screen2D::Main;
        localHumanMask = 0x3Fu;
        localPlayerMask = 0x3Fu;
        runtime::reset();
    }

    void testLocalBoundary()
    {
        using namespace monopoly;

        route.clear();
        acceptRecipient = false;

        actions::Message message{};
        message.action = actions::Type::NotifyGameStarting;
        message.toPlayer = 3;

        userinterface::processRuleMessage(message);

        expect(route.empty(), "non-local notification is not delivered");
        expect(requestedBackdrop == display::Screen2D::Invalid,
               "non-local notification cannot change backdrop");
    }

    void testGameStartingRoute()
    {
        using namespace monopoly;

        route.clear();
        acceptRecipient = true;

        actions::Message message{};
        message.action = actions::Type::NotifyGameStarting;
        message.toPlayer = rules::AllPlayers;

        userinterface::processRuleMessage(message);

        expect(requestedBackdrop == display::Screen2D::Main,
               "NotifyGameStarting requests Main");
        expect(
            route == std::vector<std::string_view>{
                "localplayers", "display", "playerselection" },
            "local ownership is updated before DISPLAY and PlayerSelection"
        );
    }

    void testPausedAndNewGameProjection()
    {
        using namespace monopoly;

        runtime::reset();

        actions::Message paused{};
        paused.action = actions::Type::NotifyGamePaused;
        paused.toPlayer = rules::AllPlayers;
        userinterface::processRuleMessage(paused);

        expect(runtime::state().gamePaused,
               "NotifyGamePaused updates portable runtime state");

        runtime::state().gameInProgress = true;
        actions::Message gameOver{};
        gameOver.action = actions::Type::NotifyGameOver;
        gameOver.toPlayer = rules::AllPlayers;
        userinterface::processRuleMessage(gameOver);
        expect(!runtime::state().gameInProgress,
               "NotifyGameOver clears legacy GameInProgress projection");

        auto& uiState = userinterface::ruleState();
        uiState.options.housesPerHotel = 9;
        uiState.squares[0].owner = 2;
        uiState.squares[0].houses = 4;
        uiState.players[0].currentSquare = 3;

        const int resetBefore = localResetCount;
        runtime::state().gameInProgress = true;

        actions::Message reset{};
        reset.action = actions::Type::NotifyNumberOfPlayers;
        reset.toPlayer = rules::AllPlayers;
        reset.numberA = 0;
        userinterface::processRuleMessage(reset);

        expect(!runtime::state().gameInProgress,
               "new-game player reset clears legacy GameInProgress projection");
        expect(uiState.numberOfPlayers == 0,
               "new-game projection has zero players");
        expect(uiState.options.housesPerHotel == 5,
               "new-game projection restores houses-per-hotel");
        expect(uiState.squares[0].owner == rules::NobodyPlayer &&
               uiState.squares[0].houses == 0,
               "new-game projection clears square ownership");
        expect(uiState.players[0].currentSquare == 41,
               "new-game projection returns players off board");
        expect(localResetCount == resetBefore + 1,
               "new-game projection resets local ownership once");
    }


    void testStartTurnQueuesHistoricalIdleTransition()
    {
        using namespace monopoly;
        userinterface::resetRuleProjection();
        runtime::reset();
        queueLockDepth = 0;
        routingDisplayState = {};
        auto& uiState = userinterface::ruleState();
        uiState.numberOfPlayers = 3;
        for (rules::PlayerNumber player = 0; player < 3; ++player)
        {
            uiState.players[player].currentSquare = 0;
            uiState.players[player].token = player;
        }

        actions::Message starting{};
        starting.action = actions::Type::NotifyGameStarting;
        starting.toPlayer = rules::AllPlayers;
        userinterface::processRuleMessage(starting);

        actions::Message turn{};
        turn.action = actions::Type::NotifyStartTurn;
        turn.toPlayer = rules::AllPlayers;
        turn.numberA = 0;
        userinterface::processRuleMessage(turn);
        auto plan = userinterface::takePendingPieceIdleTransitionPlan();

        expect(plan && !plan->movingOut && plan->movingIn &&
            plan->movingIn->restingSlot == 2,
            "first start-turn uses reversed GO slot and has no outgoing center");
        expect(queueLockDepth == 1 && uiState.currentPlayer == 0,
            "start-turn takes game-queue lock and then publishes new current player");
        expect(runtime::state().gameInProgress,
            "NotifyStartTurn sets legacy GameInProgress projection");
        expect(routingDisplayState.desiredBoardCamera == pieces::pickCameraFor3Squares(0),
            "start-turn requests the historical three-square camera before playback");
        expect(!userinterface::takePendingPieceIdleTransitionPlan(),
            "idle transition plan is consumed exactly once");
        queueLockDepth = 0;
    }
    void testHousingShortageProjectionRouting()
    {
        using namespace monopoly;
        userinterface::resetRuleProjection();
        shortageResolvedPlayer = 4;

        actions::Message houses{};
        houses.action = actions::Type::NotifyHousingShortage;
        houses.toPlayer = rules::AllPlayers;
        houses.numberA = 1;
        houses.numberC = -2;
        houses.numberE = (1u << 1) | (1u << 4);
        userinterface::processRuleMessage(houses);
        const auto& projected = userinterface::iBarRuleStateReadOnly();
        expect(projected.mode == ibar::RuleMode::HousingShort && projected.player == 4,
            "housing-shortage routing uses LocalPlayers-resolved bidder and house mode");

        shortageResolvedPlayer = rules::NobodyPlayer;
        actions::Message none = houses;
        none.numberC = 1;
        userinterface::processRuleMessage(none);
        expect(projected.mode == ibar::RuleMode::HousingShort && projected.player == 4,
            "housing-shortage routing preserves prior mode when no eligible player remains");
        shortageResolvedPlayer = rules::NobodyPlayer;
    }

    void testTradeAcceptanceProjectionRouting()
    {
        using namespace monopoly;
        userinterface::resetRuleProjection();
        tradeResolvedPlayer = 3;
        capturedTradeB = rules::NobodyPlayer;
        capturedTradePending = 0;

        actions::Message started{};
        started.action = actions::Type::NotifyTradeStarted;
        started.toPlayer = rules::AllPlayers;
        started.numberA = 1;
        userinterface::processRuleMessage(started);

        actions::Message item{};
        item.action = actions::Type::NotifyTradeItem;
        item.toPlayer = rules::AllPlayers;
        item.numberA = 1;
        item.numberB = 3;
        userinterface::processRuleMessage(item);

        actions::Message acceptance{};
        acceptance.action = actions::Type::NotifyTradeAcceptanceDecision;
        acceptance.toPlayer = rules::AllPlayers;
        acceptance.numberA = (1u << 2) | (1u << 3);
        userinterface::processRuleMessage(acceptance);

        const auto& projected = userinterface::iBarRuleStateReadOnly();
        expect(capturedTradeB == 3 && capturedTradePending == acceptance.numberA,
            "trade acceptance routing passes reconstructed TradeB and pending playerset");
        expect(projected.mode == ibar::RuleMode::Trading && projected.player == 3,
            "trade acceptance routing enters Trading for UDTrade-resolved player");

        tradeResolvedPlayer = rules::NobodyPlayer;
    }

    void testDicePromptProjection()
    {
        using namespace monopoly;
        userinterface::resetRuleProjection();
        runtime::reset();
        runtime::state().gamePaused = true;
        auto& uiState = userinterface::ruleState();
        uiState.numberOfPlayers = 1;
        uiState.currentPlayer = 0;
        uiState.players[0].currentSquare = 41;
        routingDisplayState.desiredBoardCamera = pieces::BoardCameraView::TopDownSquare;
        actions::Message prompt{};
        prompt.action=actions::Type::NotifyPleaseRollDice;
        prompt.toPlayer=rules::AllPlayers;
        userinterface::processRuleMessage(prompt);
        auto& state=userinterface::dicePromptState();
        state.show();
        const auto& iBarRules = userinterface::iBarRuleStateReadOnly();
        expect(iBarRules.mode == ibar::RuleMode::StartTurn && iBarRules.player == 0,
            "local PLEASE_ROLL_DICE also routes the exact UDIBar StartTurn mode");
        expect(state.currentStartTurn && state.diceRollNotification,
            "local PLEASE_ROLL_DICE routes the bobbing prompt and notification");
        expect(runtime::state().gameInProgress && !runtime::state().gamePaused,
            "NotifyPleaseRollDice sets GameInProgress and clears GamePaused");
        expect(routingDisplayState.desiredBoardCamera ==
                pieces::BoardCameraView::FifteenTiles04,
            "NotifyPleaseRollDice applies UDBoard roll selection including off-board fallback to square 10");

        actions::Message jail{};
        jail.action = actions::Type::NotifyJailExitChoice;
        jail.toPlayer = rules::AllPlayers;
        jail.numberA = 0;
        userinterface::processRuleMessage(jail);
        expect(routingDisplayState.desiredBoardCamera == pieces::BoardCameraView::CornerJail,
            "NotifyJailExitChoice applies the fixed UDBoard jail corner camera");

        actions::Message roll{};
        roll.action=actions::Type::NotifyDiceRolled;roll.toPlayer=rules::AllPlayers;
        roll.numberA=1;roll.numberB=2;
        userinterface::processRuleMessage(roll);
        state.show();
        expect(!state.currentStartTurn && state.diceRollNotification,
            "local DICE_ROLLED exits bobbing without losing skipped-frame notification");
        expect(iBarRules.mode == ibar::RuleMode::Nothing && iBarRules.player == 0,
            "local DICE_ROLLED clears UDIBar mode without replacing its player");
        userinterface::resetRuleProjection();queueLockDepth=0;
        expect(!state.currentStartTurn && !state.ruleStartTurn && !state.diceRollNotification,
            "rule projection reset clears pending 2D dice prompt");
        expect(iBarRules.mode == ibar::RuleMode::Nothing &&
                iBarRules.player == rules::NobodyPlayer,
            "rule projection reset also clears the UDIBar rules state");
    }

    void testDiceNotificationQueuesHistoricalRoll()
    {
        using namespace monopoly;
        userinterface::resetRuleProjection();
        queueLockDepth = 0;
        routingTick = 321;

        actions::Message roll{};
        roll.action = actions::Type::NotifyDiceRolled;
        roll.toPlayer = rules::AllPlayers;
        roll.numberA = 4;
        roll.numberB = 6;
        roll.numberC = 2;
        userinterface::processRuleMessage(roll);

        const auto& state = userinterface::ruleStateReadOnly();
        auto pending = userinterface::takePendingDiceRoll();
        expect(state.dice[0] == 4 && state.dice[1] == 6,
            "NotifyDiceRolled updates the UI dice projection");
        expect(queueLockDepth == 1,
            "NotifyDiceRolled takes exactly one game-queue lock");
        expect(pending && pending->values[0] == 4 && pending->values[1] == 6 &&
            pending->player == 2 && pending->lockTick == 321,
            "NotifyDiceRolled publishes the historical timed roll request");
        expect(!userinterface::takePendingDiceRoll(),
            "dice roll request is consumed exactly once");
        queueLockDepth = 0;
    }

    void testProposedConfigurationProjection()
    {
        using namespace monopoly;

        userinterface::resetRuleProjection();
        auto& uiState = userinterface::ruleState();
        rules::GameOptions proposed = uiState.options;
        proposed.initialCash = 2222;
        proposed.houseShortageLevel = 6;
        proposed.futureRentTradingAllowed = true;

        actions::Message notification{};
        notification.action = actions::Type::NotifyProposedConfiguration;
        notification.toPlayer = rules::AllPlayers;
        const bool encoded = rules::archive::encodeOptions(
            proposed, notification.binaryDataA);
        expect(encoded, "NotifyProposedConfiguration fixture encodes options");

        userinterface::processRuleMessage(notification);
        expect(userinterface::ruleStateReadOnly().options == proposed,
            "NotifyProposedConfiguration replaces the UI options from its validated blob");

        const auto beforeGarbage = userinterface::ruleStateReadOnly().options;
        notification.binaryDataA = {0x42, 0x00};
        userinterface::processRuleMessage(notification);
        expect(userinterface::ruleStateReadOnly().options == beforeGarbage,
            "invalid proposed configuration blob leaves the UI options unchanged");
    }

    void testFirstNonZeroPlayerProjection()
    {
        using namespace monopoly;

        userinterface::resetRuleProjection();
        auto& uiState = userinterface::ruleState();

        uiState.options.initialCash = 777;
        uiState.squares[0].owner = 2;
        uiState.squares[0].houses = 4;
        uiState.players[0].cash = 321;
        uiState.players[0].currentSquare = 3;

        const int resetBefore = localResetCount;

        actions::Message firstCount{};
        firstCount.action = actions::Type::NotifyNumberOfPlayers;
        firstCount.toPlayer = rules::AllPlayers;
        firstCount.numberA = 3;
        userinterface::processRuleMessage(firstCount);

        expect(uiState.numberOfPlayers == 3,
               "first non-zero count is retained after initialization");
        expect(uiState.options.initialCash == 777 &&
               uiState.players[0].cash == 321,
               "first non-zero count does not wipe unrelated rule data");
        expect(uiState.squares[0].owner == rules::NobodyPlayer &&
               uiState.squares[0].houses == 0,
               "first non-zero count initializes square display state");
        expect(uiState.players[0].currentSquare == 41,
               "first non-zero count returns players off board");
        expect(localResetCount == resetBefore + 1,
               "first non-zero count resets local ownership once");

        uiState.squares[0].owner = 1;
        uiState.players[0].currentSquare = 8;

        actions::Message laterCount = firstCount;
        laterCount.numberA = 4;
        userinterface::processRuleMessage(laterCount);

        expect(uiState.numberOfPlayers == 4,
               "later non-zero count updates the player count");
        expect(uiState.squares[0].owner == 1 &&
               uiState.players[0].currentSquare == 8,
               "later non-zero count does not repeat first-time initialization");
        expect(localResetCount == resetBefore + 1,
               "later non-zero count preserves local ownership");
    }
}

int main()
{
    std::cout
        << "Monopoly UserInterface routing tests\n"
        << "====================================\n";

    testUiModuleOrder();
    testAuctionBidRouting();
    testAuctionRuleRouting();
    testAuctionReadyResponses();
    testTradeEntryAndPartnerRouting();
    testLocalBoundary();
    testGameStartingRoute();
    testStartTurnQueuesHistoricalIdleTransition();
    testHousingShortageProjectionRouting();
    testTradeAcceptanceProjectionRouting();
    testDiceNotificationQueuesHistoricalRoll();
    testDicePromptProjection();
    testProposedConfigurationProjection();
    testFirstNonZeroPlayerProjection();
    testPausedAndNewGameProjection();

    if (failures != 0)
    {
        std::cerr << failures << " UserInterface test(s) failed.\n";
        return 1;
    }

    std::cout << "All UserInterface routing tests passed.\n";
    return 0;
}
