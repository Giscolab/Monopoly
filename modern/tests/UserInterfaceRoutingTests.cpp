#include "Actions.hpp"
#include "Display.hpp"
#include "RuntimeState.hpp"
#include "UserInterface.hpp"
#include "PieceCamera.hpp"
#include "RuleArchive.hpp"
#include "Messaging.hpp"
#include "ChatRuntime.hpp"
#include "PennybagsCatalog.hpp"
#include "TokenVoiceCatalog.hpp"

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
    int iBarRestoreCount = 0;
    monopoly::rules::PlayerNumber shortageResolvedPlayer = monopoly::rules::NobodyPlayer;
    monopoly::rules::PlayerNumber tradeResolvedPlayer = monopoly::rules::NobodyPlayer;
    monopoly::rules::PlayerNumber capturedTradeB = monopoly::rules::NobodyPlayer;
    std::uint32_t capturedTradePending = 0;
    std::uint64_t routingTick = 0;
    bool spokenPostLockSlotEmptyResult = true;
    bool usaBoardEditionResult = true;
    int jailChoiceHostCommentCount = 0;
    std::optional<monopoly::udsound::PennybagsVoice> lastPennybagsVoice;
    std::optional<monopoly::udsound::TokenVoiceClipPolicy> lastPennybagsPolicy;
    std::uint32_t localHumanMask = 0x3F;
    std::uint32_t localPlayerMask = 0x3F;
    monopoly::rules::PlayerNumber selectedLocalUIPlayer = monopoly::rules::NobodyPlayer;
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

    std::size_t routeCount(std::string_view value)
    {
        std::size_t count{};
        for (const auto item : route)
            if (item == value) ++count;
        return count;
    }

    void expect(bool condition, std::string_view description)
    {
        if (condition)
        {
            std::cout << "[PASS] " << description << '\r\n';
            return;
        }

        ++failures;
        std::cerr << "[FAIL] " << description << '\r\n';
    }

    void appendU32(std::vector<std::uint8_t>& data, std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            data.push_back(static_cast<std::uint8_t>(value >> shift));
    }

    void appendI64(std::vector<std::uint8_t>& data, std::int64_t value)
    {
        const auto raw = static_cast<std::uint64_t>(value);
        for (int shift = 0; shift < 64; shift += 8)
            data.push_back(static_cast<std::uint8_t>(raw >> shift));
    }

    std::vector<std::uint8_t> makeResyncBlob()
    {
        using namespace monopoly;
        std::vector<std::uint8_t> data{1};
        for (rules::PlayerNumber p = 0; p < rules::MaxPlayers; ++p)
            appendI64(data, 1000 + p);
        const std::uint32_t bit = 1u;
        for (rules::PlayerNumber p = 0; p < rules::MaxPlayers; ++p)
            appendU32(data, p == 2 ? bit : 0);
        appendU32(data, bit);
        for (rules::PlayerNumber p = 0; p < rules::MaxPlayers; ++p)
            data.push_back(static_cast<std::uint8_t>(p + 1));
        data.push_back(2);
        data.push_back(rules::NobodyPlayer);
        for (std::size_t square = 0; square < rules::SquareCount; ++square)
            data.push_back(square == 1 ? 3 : 0);
        data.push_back(0);
        data.push_back(0);
        appendU32(data, (1u << 1) | (1u << 3));
        data.push_back(2);
        return data;
    }
}

namespace monopoly::ibar
{
    RuleMode resolveRuleMode(RuleMode projectedMode, rules::PlayerNumber) noexcept
    { return projectedMode; }
    rules::PlayerNumber resolveRulePlayer(rules::PlayerNumber projectedPlayer) noexcept
    { return projectedPlayer; }
    void restoreRuleTracking() noexcept { ++iBarRestoreCount; }
}

namespace monopoly::engine
{
    void playWarningSound() noexcept { route.push_back("warning"); }
    void playClickSound() noexcept { route.push_back("click"); }
    void playTokenVoice(std::uint8_t, udsound::TokenVoiceLine,
        udsound::TokenVoiceClipPolicy, bool) noexcept
    {
        route.push_back("tokenvoice");
    }
    void playPennybagsVoice(udsound::PennybagsVoice voice,
        udsound::TokenVoiceClipPolicy policy, bool) noexcept
    {
        lastPennybagsVoice = voice;
        lastPennybagsPolicy = policy;
        route.push_back("pennybags");
    }
    bool spokenPostLockSlotEmpty() noexcept { return spokenPostLockSlotEmptyResult; }
    bool isUsaBoardEdition() noexcept { return usaBoardEditionResult; }
    void playJailChoiceHostComment() noexcept
    {
        ++jailChoiceHostCommentCount;
        route.push_back("jailchoice");
    }
}

namespace monopoly::display
{
    State& state()
    {
        return routingDisplayState;
    }

    const State& stateReadOnly()
    {
        return routingDisplayState;
    }

    void applyMusicTune(std::uint8_t tuneIndex) noexcept
    {
        routingDisplayState.optionMusicTuneIndex = tuneIndex;
    }

    void applyTokenVoicesOption(bool enabled) noexcept
    {
        routingDisplayState.optionTokenVoicesOn = enabled;
    }

    void applyHostCommentsOption(bool enabled) noexcept
    {
        routingDisplayState.optionHostCommentsOn = enabled;
    }

    void applyMusicOption(bool musicOn) noexcept
    {
        routingDisplayState.optionMusicOn = musicOn;
    }

    void applyRuntimeOptions(bool tokenAnimationsOn, bool cameraMovementOn,
        bool lightingOn, bool board3DOn) noexcept
    {
        routingDisplayState.optionTokenAnimationsOn = tokenAnimationsOn;
        routingDisplayState.optionCameraMovementOn = cameraMovementOn;
        routingDisplayState.optionLightingOn = lightingOn;
        routingDisplayState.game3DOn = board3DOn;
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

    rules::PlayerNumber currentUIPlayer()
    {
        return selectedLocalUIPlayer;
    }

    void setCurrentUIPlayerFromPlayerSet(
        const rules::GameState& state, std::uint32_t playerSet)
    {
        selectedLocalUIPlayer = rules::NobodyPlayer;
        for (rules::PlayerNumber player = 0; player < state.numberOfPlayers; ++player)
        {
            if ((playerSet & (1u << player)) != 0 && slotIsLocalHumanPlayer(player))
            {
                selectedLocalUIPlayer = player;
                break;
            }
        }
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
    bool networkMode()
    {
        return false;
    }

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


namespace monopoly::chat
{
    void reset() noexcept {}

    bool processInput(const uimsg::Message&, rules::PlayerNumber, std::uint32_t, bool)
    {
        return false;
    }

    bool processRuleMessage(const actions::Message&)
    {
        return false;
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

    void testOptionsEntryAndCancelRouting()
    {
        using namespace monopoly;
        runtime::reset();
        runtime::state().gameInProgress = true;
        userinterface::resetRuleProjection();
        routingDisplayState.current2DView = display::Screen2D::Trade;
        routingDisplayState.desired2DView = display::Screen2D::Trade;
        requestedBackdrop = display::Screen2D::Invalid;

        expect(userinterface::beginOptionsFromIBar() &&
                requestedBackdrop == display::Screen2D::Options &&
                userinterface::optionsStateReadOnly().active &&
                userinterface::optionsStateReadOnly().previousView == display::Screen2D::Trade,
            "IBar Options entry records current Trade view and requests Options backdrop");

        route.clear();
        uimsg::Message cancel{};
        cancel.type = uimsg::Type::MouseLeftDown;
        cancel.numberA = 300;
        cancel.numberB = 420;
        expect(userinterface::processUIMessage(cancel) &&
                requestedBackdrop == display::Screen2D::Trade &&
                !userinterface::optionsStateReadOnly().active,
            "UDOpts File Cancel returns through UserInterface to saved IBar view");
        runtime::reset();
    }

    void testOptionsSupportedToggleRouting()
    {
        using namespace monopoly;
        runtime::reset();
        runtime::state().gameInProgress = true;
        userinterface::resetRuleProjection();
        routingDisplayState = {};
        routingDisplayState.current2DView = display::Screen2D::Trade;
        routingDisplayState.desired2DView = display::Screen2D::Trade;
        routingDisplayState.optionTokenVoicesOn = true;
        routingDisplayState.optionHostCommentsOn = true;
        routingDisplayState.optionTokenAnimationsOn = true;
        routingDisplayState.optionCameraMovementOn = true;
        routingDisplayState.optionLightingOn = true;
        routingDisplayState.game3DOn = true;
        requestedBackdrop = display::Screen2D::Invalid;

        expect(userinterface::beginOptionsFromIBar(),
            "Options runtime-toggle fixture enters from Trade");
        const auto tabRect = optionsui::menuButtonRect(optionsui::MenuButton::Option);
        uimsg::Message tab{};
        tab.type = uimsg::Type::MouseLeftDown;
        tab.numberA = (tabRect.left + tabRect.right) / 2;
        tab.numberB = tabRect.top + 1;
        expect(userinterface::processUIMessage(tab) &&
                userinterface::optionsStateReadOnly().optionSnapshotLoaded &&
                userinterface::optionsStateReadOnly().optionOn[
                    static_cast<std::size_t>(optionsui::OptionToggle::Camera)] &&
                userinterface::optionsStateReadOnly().optionOn[
                    static_cast<std::size_t>(optionsui::OptionToggle::HostComments)],
            "entering Option tab snapshots supported DISPLAY runtime owners");

        const auto cameraRect = optionsui::optionToggleRect(
            optionsui::OptionToggle::Camera, true);
        uimsg::Message toggle{};
        toggle.type = uimsg::Type::MouseLeftDown;
        toggle.numberA = cameraRect.left + 1;
        toggle.numberB = cameraRect.top + 1;
        expect(userinterface::processUIMessage(toggle) &&
                !userinterface::optionsStateReadOnly().optionOn[
                    static_cast<std::size_t>(optionsui::OptionToggle::Camera)] &&
                routingDisplayState.optionCameraMovementOn,
            "Option toggle remains temporary until retail OK is pressed");

        const auto hostRect = optionsui::optionToggleRect(
            optionsui::OptionToggle::HostComments, true);
        toggle.numberA = hostRect.left + 1;
        toggle.numberB = hostRect.top + 1;
        expect(userinterface::processUIMessage(toggle) &&
                !userinterface::optionsStateReadOnly().optionOn[
                    static_cast<std::size_t>(optionsui::OptionToggle::HostComments)] &&
                routingDisplayState.optionHostCommentsOn,
            "Host Comments toggle remains temporary until retail OK is pressed");

        uimsg::Message okay{};
        okay.type = uimsg::Type::MouseLeftDown;
        okay.numberA = 350;
        okay.numberB = 450;
        expect(userinterface::processUIMessage(okay) &&
                !routingDisplayState.optionCameraMovementOn &&
                !routingDisplayState.optionHostCommentsOn &&
                requestedBackdrop == display::Screen2D::Trade &&
                !userinterface::optionsStateReadOnly().active,
            "Option OK applies supported runtime values then restores saved IBar view");
        runtime::reset();
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
        expect(std::count(route.begin(), route.end(), "pennybags") == 1,
            "fresh 3+ player Trade entry plays PickTradePartner host comment exactly once");
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
        const auto beforeMissingOffer = std::count(route.begin(), route.end(), "pennybags");
        uimsg::Message propose{};
        propose.type = uimsg::Type::MouseLeftDown;
        propose.numberA = 203;
        propose.numberB = 421;
        expect(userinterface::processUIMessage(propose) &&
                std::count(route.begin(), route.end(), "pennybags") == beforeMissingOffer + 1,
            "incomplete Propose click routes the retail Pennybags warning");

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
        expect(std::count(route.begin(), route.end(), "pennybags") == beforeMissingOffer + 1,
            "reopening stored Trade does not replay the entry host comment");

        requestedBackdrop = display::Screen2D::Invalid;
        routingDisplayState.desired2DView = display::Screen2D::Main;
        localHumanMask = 0x3Fu;
        localPlayerMask = 0x3Fu;
        runtime::reset();
    }

    void testTradeEditorSubmissionPreflightsQueue()
    {
        using namespace monopoly;

        userinterface::resetRuleProjection();
        acceptRecipient = true;
        acceptMessaging = true;
        localHumanMask = 0x01u;
        localPlayerMask = 0x01u;
        auto& uiState = userinterface::ruleState();
        uiState.numberOfPlayers = 2;
        uiState.players[0].currentSquare = 0;
        uiState.players[1].currentSquare = 1;

        auto& trade = userinterface::tradeState();
        trade.playerA = 0;
        trade.playerB = 1;
        trade.tradeFrom = 0;
        actions::Message first{};
        first.action = actions::Type::TradeItem;
        first.fromPlayer = 0;
        first.toPlayer = rules::BankPlayer;
        first.numberA = 0;
        first.numberB = 1;
        first.numberC = static_cast<std::int64_t>(rules::TradeItemKind::Cash);
        first.numberD = 25;
        actions::Message second = first;
        second.numberC = static_cast<std::int64_t>(rules::TradeItemKind::Square);
        second.numberD = 6;
        trade.items = {first, second};

        actions::Message editor{};
        editor.action = actions::Type::NotifyTradeEditor;
        editor.toPlayer = rules::AllPlayers;
        editor.numberA = 0;

        capturedMessages.clear();
        simulatedQueuedActions = messaging::MessageQueueCapacity - 2;
        userinterface::processRuleMessage(editor);
        expect(capturedMessages.empty() &&
                simulatedQueuedActions == messaging::MessageQueueCapacity - 2,
            "Trade_SendItems preflights the whole item+done batch before emitting anything");

        capturedMessages.clear();
        simulatedQueuedActions = messaging::MessageQueueCapacity - 3;
        userinterface::processRuleMessage(editor);
        expect(capturedMessages.size() == 3 &&
                capturedMessages[0].action == actions::Type::TradeItem &&
                capturedMessages[1].action == actions::Type::TradeItem &&
                capturedMessages[2].action == actions::Type::TradeEditingDone &&
                capturedMessages[2].numberA == 0 &&
                capturedMessages[2].numberB == 1 &&
                simulatedQueuedActions == messaging::MessageQueueCapacity,
            "Trade_SendItems emits the ordered retail batch when the FIFO has exact capacity");

        simulatedQueuedActions = 0;
        capturedMessages.clear();
        localHumanMask = 0x3Fu;
        localPlayerMask = 0x3Fu;
    }

    void testUDPennyVoiceRouting()
    {
        using namespace monopoly;
        userinterface::resetRuleProjection();
        acceptRecipient = true;
        auto& state = userinterface::ruleState();
        state.numberOfPlayers = 2;
        state.currentPlayer = 0;
        state.players[0].token = 2;
        state.players[0].aiPlayerLevel = 1;
        state.players[0].currentSquare = 1;
        route.clear();

        actions::Message completed{};
        completed.action = actions::Type::NotifyActionCompleted;
        completed.toPlayer = rules::AllPlayers;
        completed.numberA = static_cast<std::int64_t>(
            actions::Type::BuyOrAuctionDecision);
        completed.numberB = 1;
        completed.numberC = 0;
        completed.numberD = 1;
        userinterface::processRuleMessage(completed);
        expect(std::find(route.begin(), route.end(), "tokenvoice") != route.end(),
            "accepted AI property purchase routes UDPenny token voice");

        route.clear();
        completed.numberD = 0;
        userinterface::processRuleMessage(completed);
        expect(std::find(route.begin(), route.end(), "tokenvoice") != route.end(),
            "accepted AI auction choice routes UDPenny token voice");

        state.players[1].aiPlayerLevel = 0;
        state.players[1].currentSquare = 6;
        route.clear();
        completed.numberC = 1;
        completed.numberD = 1;
        userinterface::processRuleMessage(completed);
        expect(std::find(route.begin(), route.end(), "pennybags") != route.end(),
            "accepted human property purchase routes Pennybags host comment");

        route.clear();
        localHumanMask = 0;
        state.players[0].currentSquare = 30;
        actions::Message jail{};
        jail.action = actions::Type::NotifyJumpToSquare;
        jail.toPlayer = rules::AllPlayers;
        jail.numberA = 40;
        jail.numberC = 0;
        userinterface::processRuleMessage(jail);
        expect(std::find(route.begin(), route.end(), "tokenvoice") != route.end(),
            "GoToJail movement routes UDPenny token voice");

        userinterface::resetRuleProjection();
        state.numberOfPlayers = 2;
        state.currentPlayer = 1;
        state.players[1].token = 3;
        state.players[1].aiPlayerLevel = 0;
        route.clear();
        localHumanMask = 0x3Fu;
        state.players[1].currentSquare = 30;
        jail.numberC = 1;
        userinterface::processRuleMessage(jail);
        expect(std::find(route.begin(), route.end(), "pennybags") != route.end(),
            "local human GoToJail routes Pennybags negative host comment");
        localHumanMask = 0x3Fu;
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

    void testError71HostComments()
    {
        using namespace monopoly;
        acceptRecipient = true;
        route.clear();
        lastPennybagsVoice.reset();
        lastPennybagsPolicy.reset();

        actions::Message error{};
        error.action = actions::Type::NotifyErrorMessage;
        error.toPlayer = rules::AllPlayers;
        error.numberA = 71;
        error.numberC = 6;
        userinterface::processRuleMessage(error);
        expect(routeCount("warning") == 1 && routeCount("pennybags") == 0,
            "error 71/6 keeps the retail host-left warning sound");

        route.clear();
        error.numberC = 1;
        userinterface::processRuleMessage(error);
        expect(routeCount("warning") == 0 && routeCount("pennybags") == 1 &&
                lastPennybagsVoice == udsound::PennybagsVoice::HumanReplacedByComputerPlayer &&
                lastPennybagsPolicy == udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay,
            "other error 71 variants play HumanReplacedByComputerPlayer");
    }

    void testGameStartingRoute()
    {
        using namespace monopoly;

        route.clear();
        acceptRecipient = true;

        auto& uiState = userinterface::ruleState();
        uiState.countHits[0].toPlayer = 1;
        uiState.countHits[0].tradedItem = true;
        const int restoreBefore = iBarRestoreCount;

        actions::Message message{};
        message.action = actions::Type::NotifyGameStarting;
        message.toPlayer = rules::AllPlayers;

        userinterface::processRuleMessage(message);

        expect(requestedBackdrop == display::Screen2D::Main,
               "NotifyGameStarting requests Main");
        expect(uiState.countHits[0].toPlayer == rules::NobodyPlayer &&
               !uiState.countHits[0].tradedItem,
               "NotifyGameStarting clears stale retail CountHits");
        expect(iBarRestoreCount == restoreBefore + 1,
               "NotifyGameStarting restores retail IBar tracking");
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
        localHumanMask = (1u << 2);
        selectedLocalUIPlayer = rules::NobodyPlayer;
        userinterface::ruleState().numberOfPlayers = 3;
        route.clear();
        lastPennybagsVoice.reset();
        lastPennybagsPolicy.reset();
        requestedBackdrop = display::Screen2D::Invalid;
        actions::Message gameOver{};
        gameOver.action = actions::Type::NotifyGameOver;
        gameOver.toPlayer = rules::AllPlayers;
        userinterface::processRuleMessage(gameOver);
        expect(!runtime::state().gameInProgress,
               "NotifyGameOver clears legacy GameInProgress projection");
        expect(routeCount("pennybags") == 1 &&
                lastPennybagsVoice == udsound::PennybagsVoice::PlayAgain &&
                lastPennybagsPolicy == udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay,
               "first active-game NotifyGameOver plays retail PlayAgain comment");
        expect(requestedBackdrop == display::Screen2D::Main,
               "first active-game NotifyGameOver forces the retail Main backdrop");
        expect(selectedLocalUIPlayer == 2,
               "first active-game NotifyGameOver selects the first local human");
        userinterface::processRuleMessage(gameOver);
        localHumanMask = localPlayerMask = 0x3Fu;
        expect(routeCount("pennybags") == 1,
               "duplicate NotifyGameOver stays silent after GameInProgress clears");

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
        route.clear();
        runtime::state().gamePaused = true;
        userinterface::processRuleMessage(turn);
        expect(!runtime::state().gamePaused,
            "NotifyStartTurn clears the retail paused state");
        expect(std::find(route.begin(), route.end(), "pennybags") != route.end() &&
               std::find(route.begin(), route.end(), "tokenvoice") != route.end(),
            "first NotifyStartTurn routes Pennybags roll prompt then token intro");
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
        route.clear();
        userinterface::processRuleMessage(houses);
        expect(std::find(route.begin(), route.end(), "pennybags") != route.end(),
            "first local housing-shortage countdown-zero message plays Pennybags instruction");
        route.clear();
        userinterface::processRuleMessage(houses);
        expect(std::find(route.begin(), route.end(), "pennybags") == route.end(),
            "duplicate countdown-zero housing-shortage message is silent like retail lastCount guard");
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

    void testRaiseMoneyAndJailHostComments()
    {
        using namespace monopoly;
        userinterface::resetRuleProjection();
        auto& uiState = userinterface::ruleState();
        uiState.numberOfPlayers = 1;
        uiState.players[0].currentSquare = 1;
        routingDisplayState.desired2DView = display::Screen2D::Main;
        localHumanMask = localPlayerMask = 0x01u;
        spokenPostLockSlotEmptyResult = true;
        route.clear();

        actions::Message debt{};
        debt.action = actions::Type::NotifyPleasePay;
        debt.toPlayer = rules::AllPlayers;
        debt.numberA = 0;
        debt.numberC = 500;
        debt.numberE = 1;
        routingTick = 2400;
        userinterface::processRuleMessage(debt);
        expect(routeCount("pennybags") == 0, "RaiseMoney stays silent at exact 40-second threshold");
        routingTick = 2401;
        userinterface::processRuleMessage(debt);
        expect(routeCount("pennybags") == 1,
            "RaiseMoney plays suggestion strictly after 2400 ticks");
        routingTick = 4801;
        userinterface::update();
        expect(routeCount("pennybags") == 1,
            "RaiseMoney periodic reminder keeps strict greater-than threshold");
        routingTick = 4802;
        userinterface::update();
        expect(routeCount("pennybags") == 2,
            "RaiseMoney periodic UI maintenance repeats after another 2400 ticks");
        spokenPostLockSlotEmptyResult = false;
        routingTick = 7203;
        userinterface::update();
        expect(routeCount("pennybags") == 2,
            "RaiseMoney does not overwrite an occupied post-lock voice slot");
        spokenPostLockSlotEmptyResult = true;
        routingDisplayState.desired2DView = display::Screen2D::Options;
        userinterface::update();
        expect(routeCount("pennybags") == 2,
            "RaiseMoney reminder stays silent while the retail IBar is hidden");
        routingDisplayState.desired2DView = display::Screen2D::Main;
        userinterface::update();
        expect(routeCount("pennybags") == 3,
            "RaiseMoney reminder fires immediately when the IBar becomes visible");

        route.clear();
        jailChoiceHostCommentCount = 0;
        actions::Message jail{};
        jail.action = actions::Type::NotifyJailExitChoice;
        jail.toPlayer = rules::AllPlayers;
        jail.numberA = 0;
        jail.numberB = 1;
        userinterface::processRuleMessage(jail);
        expect(jailChoiceHostCommentCount == 1 && routeCount("jailchoice") == 1,
            "local human who may roll out of jail gets edition-aware host comment");
        jail.numberB = 0;
        userinterface::processRuleMessage(jail);
        expect(jailChoiceHostCommentCount == 1,
            "forced-pay jail choice does not play roll-or-pay host comment");
        localHumanMask = 0;
        jail.numberB = 1;
        userinterface::processRuleMessage(jail);
        expect(jailChoiceHostCommentCount == 1,
            "non-local jail choice stays silent");
        localHumanMask = localPlayerMask = 0x3Fu;
        spokenPostLockSlotEmptyResult = true;
        routingTick = 0;
    }

    void testBasicGameStateProjection()
    {
        using namespace monopoly;
        userinterface::resetRuleProjection();
        auto& uiState = userinterface::ruleState();
        uiState.numberOfPlayers = 2;

        actions::Message cash{};
        cash.action = actions::Type::NotifyCashAmount;
        cash.toPlayer = rules::AllPlayers;
        cash.numberA = 1;
        cash.numberC = 1234;
        userinterface::processRuleMessage(cash);
        expect(uiState.players[1].cash == 1234,
            "NotifyCashAmount updates the retail player cash projection");

        actions::Message mortgage{};
        mortgage.action = actions::Type::NotifySquareMortgage;
        mortgage.toPlayer = rules::AllPlayers;
        mortgage.numberA = 3;
        mortgage.numberB = 1;
        userinterface::processRuleMessage(mortgage);
        expect(uiState.squares[3].mortgaged,
            "NotifySquareMortgage sets the retail square mortgage projection");
        mortgage.numberB = 0;
        userinterface::processRuleMessage(mortgage);
        expect(!uiState.squares[3].mortgaged,
            "NotifySquareMortgage clears the retail square mortgage projection");

        actions::Message pot{};
        pot.action = actions::Type::NotifyFreeParkingPot;
        pot.toPlayer = rules::AllPlayers;
        pot.numberA = 987;
        userinterface::processRuleMessage(pot);
        expect(uiState.freeParkingJackpotAmount == 987,
            "NotifyFreeParkingPot updates the retail jackpot projection");

        actions::Message jailCard{};
        jailCard.action = actions::Type::NotifyJailCardOwnership;
        jailCard.toPlayer = rules::AllPlayers;
        jailCard.numberA = 1;
        jailCard.numberB = static_cast<std::int64_t>(rules::DeckType::Chance);
        userinterface::processRuleMessage(jailCard);
        expect(uiState.cards[static_cast<std::size_t>(rules::DeckType::Chance)].jailOwner == 1,
            "NotifyJailCardOwnership updates the exact retail deck owner");
        jailCard.numberA = rules::NobodyPlayer;
        userinterface::processRuleMessage(jailCard);
        expect(uiState.cards[static_cast<std::size_t>(rules::DeckType::Chance)].jailOwner ==
                rules::NobodyPlayer,
            "NotifyJailCardOwnership clears the retail deck owner when the card returns");

        actions::Message immunity{};
        immunity.action = actions::Type::NotifyImmunityCount;
        immunity.toPlayer = rules::AllPlayers;
        immunity.numberA = 1;
        immunity.numberB = 4;
        immunity.numberD = 0;
        immunity.numberE = 0x24;
        userinterface::processRuleMessage(immunity);
        const auto& storedImmunity = uiState.countHits[0];
        expect(storedImmunity.fromPlayer == 0 && storedImmunity.toPlayer == 1 &&
                storedImmunity.hitType == rules::CountHitType::RentImmunity &&
                storedImmunity.hitCount == 4 && storedImmunity.properties == 0x24 &&
                !storedImmunity.tradedItem,
            "NotifyImmunityCount projects the retail AddUiImmunity record");

        immunity.numberB = 0;
        userinterface::processRuleMessage(immunity);
        expect(uiState.countHits[0].toPlayer == rules::NobodyPlayer &&
                !uiState.countHits[0].tradedItem,
            "zero NotifyImmunityCount clears the matching retail record");

        actions::Message future = immunity;
        future.action = actions::Type::NotifyFutureRentCount;
        future.numberA = 0;
        future.numberB = 3;
        future.numberD = 1;
        future.numberE = 0x18;
        userinterface::processRuleMessage(future);
        expect(uiState.countHits[0].fromPlayer == 1 && uiState.countHits[0].toPlayer == 0 &&
                uiState.countHits[0].hitType == rules::CountHitType::FutureRent &&
                uiState.countHits[0].hitCount == 3 && uiState.countHits[0].properties == 0x18,
            "NotifyFutureRentCount projects the retail future-rent record");
    }


    void testClientResyncProjection()
    {
        using namespace monopoly;
        userinterface::resetRuleProjection();
        auto& uiState = userinterface::ruleState();
        uiState.numberOfPlayers = 4;
        uiState.players[0].cash = 77;

        actions::Message resync{};
        resync.action = actions::Type::NotifyClientResyncInfo;
        resync.toPlayer = rules::AllPlayers;
        resync.binaryDataA = makeResyncBlob();
        userinterface::processRuleMessage(resync);

        expect(uiState.players[0].cash == 1000 && uiState.players[5].cash == 1005,
            "NotifyClientResyncInfo restores all retail cash values");
        expect(uiState.squares[1].owner == 2 && uiState.squares[1].mortgaged &&
               uiState.squares[1].houses == 3,
            "NotifyClientResyncInfo restores ownership mortgage and buildings");
        expect(uiState.players[3].currentSquare == 4 &&
               uiState.cards[0].jailOwner == 2 &&
               uiState.cards[1].jailOwner == rules::NobodyPlayer,
            "NotifyClientResyncInfo restores positions and jail-card owners");
        expect(uiState.players[1].firstMoveMade && uiState.players[3].firstMoveMade &&
               !uiState.players[0].firstMoveMade && uiState.currentPlayer == 2,
            "NotifyClientResyncInfo restores first-move flags and current player");

        const auto cashBefore = uiState.players[0].cash;
        const auto ownerBefore = uiState.squares[1].owner;
        resync.binaryDataA.pop_back();
        userinterface::processRuleMessage(resync);
        expect(uiState.players[0].cash == cashBefore &&
               uiState.squares[1].owner == ownerBefore,
            "truncated NotifyClientResyncInfo is rejected atomically");
    }

    void testFirstHouseCommentRouting()
    {
        using namespace monopoly;
        userinterface::resetRuleProjection();
        auto& uiState = userinterface::ruleState();
        uiState.numberOfPlayers = 1;
        route.clear();
        lastPennybagsVoice.reset();
        lastPennybagsPolicy.reset();

        actions::Message ownership{};
        ownership.action = actions::Type::NotifySquareOwnership;
        ownership.toPlayer = rules::AllPlayers;
        ownership.numberA = 1;
        ownership.numberB = 0;
        userinterface::processRuleMessage(ownership);
        expect(uiState.squares[1].owner == 0,
            "square ownership projection supplies the retail BSSM owner");

        actions::Message houses{};
        houses.action = actions::Type::NotifySquareHouses;
        houses.toPlayer = rules::AllPlayers;
        houses.numberA = 1;
        houses.numberB = 1;
        houses.numberC = 5;
        routingTick = 1800;
        userinterface::processRuleMessage(houses);
        expect(routeCount("pennybags") == 0,
            "first-house comment stays silent at exact 30-second threshold");
        expect(uiState.squares[1].houses == 1 && uiState.options.housesPerHotel == 5,
            "house notification updates the retail square projection before playback");

        houses.numberB = 2;
        routingTick = 1801;
        userinterface::processRuleMessage(houses);
        expect(routeCount("pennybags") == 1 &&
                lastPennybagsVoice == udsound::PennybagsVoice::PlayerBuiltFirstHouse &&
                lastPennybagsPolicy == udsound::TokenVoiceClipPolicy::SkipIfOldSoundPlaying,
            "first eligible build plays retail PlayerBuiltFirstHouse with skip policy");

        houses.numberB = 3;
        routingTick = 3601;
        userinterface::processRuleMessage(houses);
        expect(routeCount("pennybags") == 1,
            "later house at exact 30-second threshold stays silent");
        houses.numberB = 4;
        routingTick = 3602;
        userinterface::processRuleMessage(houses);
        expect(routeCount("pennybags") == 1,
            "later eligible house only rearms the BSSM timer without another comment");

        houses.numberB = 3;
        routingTick = 6005;
        userinterface::processRuleMessage(houses);
        expect(routeCount("pennybags") == 1,
            "selling a house never emits the first-house comment");
        routingTick = 0;
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

        route.clear();
        actions::Message finished{};
        finished.action = actions::Type::NotifyTradeFinished;
        finished.toPlayer = rules::AllPlayers;
        finished.numberA = 1;
        userinterface::processRuleMessage(finished);
        finished.numberA = 0;
        userinterface::processRuleMessage(finished);
        expect(std::count(route.begin(), route.end(), "pennybags") == 2,
            "accepted and rejected TradeFinished notifications route their retail host comments");

        tradeResolvedPlayer = rules::NobodyPlayer;
    }

    void testTradeInitiatorHostComments()
    {
        using namespace monopoly;
        userinterface::resetRuleProjection();
        auto& uiState = userinterface::ruleState();
        uiState.numberOfPlayers = 2;
        uiState.players[0].aiPlayerLevel = 0;
        uiState.players[1].aiPlayerLevel = 0;
        localPlayerMask = localHumanMask = 1u << 1u;
        tradeResolvedPlayer = 1;
        usaBoardEditionResult = true;
        route.clear();
        lastPennybagsVoice.reset();
        lastPennybagsPolicy.reset();

        actions::Message started{};
        started.action = actions::Type::NotifyTradeStarted;
        started.toPlayer = rules::AllPlayers;
        started.numberA = 0;
        userinterface::processRuleMessage(started);
        actions::Message item{};
        item.action = actions::Type::NotifyTradeItem;
        item.toPlayer = rules::AllPlayers;
        item.numberA = 0;
        item.numberB = 1;
        userinterface::processRuleMessage(item);

        actions::Message acceptance{};
        acceptance.action = actions::Type::NotifyTradeAcceptanceDecision;
        acceptance.toPlayer = rules::AllPlayers;
        acceptance.numberA = 1u << 1u;
        routingTick = 300;
        userinterface::processRuleMessage(acceptance);
        expect(routeCount("pennybags") == 0,
            "trade initiator comment stays silent at exact five-second threshold");

        routingTick = 301;
        userinterface::processRuleMessage(acceptance);
        expect(routeCount("pennybags") == 1 &&
                lastPennybagsVoice == udsound::PennybagsVoice::HumanInitiatesTrade &&
                lastPennybagsPolicy == udsound::TokenVoiceClipPolicy::ClipOldSoundIfPlaying,
            "remote human proposing to local human gets retail HumanInitiatesTrade");
        routingTick = 601;
        userinterface::processRuleMessage(acceptance);
        expect(routeCount("pennybags") == 1,
            "trade initiator comment keeps strict greater-than repeat threshold");
        routingTick = 602;
        userinterface::processRuleMessage(acceptance);
        expect(routeCount("pennybags") == 2,
            "trade initiator comment may repeat after more than five seconds");

        uiState.players[0].aiPlayerLevel = 1;
        routingTick = 903;
        userinterface::processRuleMessage(acceptance);
        expect(routeCount("pennybags") == 3 &&
                lastPennybagsVoice == udsound::PennybagsVoice::AIInitiatesTrade,
            "AI proposing to local player gets retail AIInitiatesTrade");

        uiState.players[1].aiPlayerLevel = 1;
        localPlayerMask = localHumanMask = 0;
        tradeResolvedPlayer = rules::NobodyPlayer;
        routingTick = 1204;
        userinterface::processRuleMessage(acceptance);
        expect(routeCount("pennybags") == 4 &&
                lastPennybagsVoice == udsound::PennybagsVoice::AIInitiatesTrade,
            "non-local AI versus AI trade keeps spectator AIInitiatesTrade comment");

        usaBoardEditionResult = false;
        routingTick = 1505;
        userinterface::processRuleMessage(acceptance);
        expect(routeCount("pennybags") == 4,
            "Europe edition suppresses USA-only trade initiator comments");
        usaBoardEditionResult = true;
        userinterface::processRuleMessage(acceptance);
        expect(routeCount("pennybags") == 5,
            "Europe suppression does not consume the USA five-second sound gate");

        localPlayerMask = localHumanMask = 0x3Fu;
        tradeResolvedPlayer = rules::NobodyPlayer;
        usaBoardEditionResult = true;
        routingTick = 0;
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

    void testCardSeenLandingGuard()
    {
        using namespace monopoly;
        userinterface::resetRuleProjection();
        routingDisplayState.justReadACardHack = false;
        actions::Message completed{};
        completed.action = actions::Type::NotifyActionCompleted;
        completed.toPlayer = rules::AllPlayers;
        completed.numberA = static_cast<std::int64_t>(actions::Type::CardSeen);
        completed.numberB = 1;
        userinterface::processRuleMessage(completed);
        expect(routingDisplayState.justReadACardHack,
            "accepted CardSeen arms retail JustReadACard landing guard");
        userinterface::resetRuleProjection();
        expect(!routingDisplayState.justReadACardHack,
            "rule projection reset clears JustReadACard landing guard");
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
        << "Monopoly UserInterface routing tests\r\n"
        << "====================================\r\n";

    testUiModuleOrder();
    testOptionsEntryAndCancelRouting();
    testOptionsSupportedToggleRouting();
    testAuctionBidRouting();
    testAuctionRuleRouting();
    testAuctionReadyResponses();
    testTradeEntryAndPartnerRouting();
    testTradeEditorSubmissionPreflightsQueue();
    testUDPennyVoiceRouting();
    testLocalBoundary();
    testError71HostComments();
    testGameStartingRoute();
    testStartTurnQueuesHistoricalIdleTransition();
    testHousingShortageProjectionRouting();
    testRaiseMoneyAndJailHostComments();
    testBasicGameStateProjection();
    testClientResyncProjection();
    testFirstHouseCommentRouting();
    testTradeAcceptanceProjectionRouting();
    testTradeInitiatorHostComments();
    testDiceNotificationQueuesHistoricalRoll();
    testDicePromptProjection();
    testCardSeenLandingGuard();
    testProposedConfigurationProjection();
    testFirstNonZeroPlayerProjection();
    testPausedAndNewGameProjection();

    if (failures != 0)
    {
        std::cerr << failures << " UserInterface test(s) failed.\r\n";
        return 1;
    }

    std::cout << "All UserInterface routing tests passed.\r\n";
    return 0;
}
