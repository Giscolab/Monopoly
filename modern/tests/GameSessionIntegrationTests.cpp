#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "AIMessageIngress.hpp"
#include "AuctionUI.hpp"
#include "BoardRules.hpp"
#include "ChatRuntime.hpp"
#include "Display.hpp"
#include "Engine.hpp"
#include "ExtendedInitialization.hpp"
#include "IBar.hpp"
#include "IBarBackdropPlayback.hpp"
#include "LegacyTextIds.hpp"
#include "LocalPlayers.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"
#include "PlayerSelection.hpp"
#include "PennybagsCatalog.hpp"
#include "RuleArchive.hpp"
#include "RuleConfiguration.hpp"
#include "RuleOptions.hpp"
#include "RuleRandom.hpp"
#include "RulesEngine.hpp"
#include "RuntimeState.hpp"
#include "TimeStep.hpp"
#include "TcpMessageTransport.hpp"
#include "TokenVoiceCatalog.hpp"
#include "UserInterface.hpp"

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

// Production RULE, FIFO, notification projection, IBar input, local ownership and AI are
// linked. Only presentation, wall-clock pacing and player-setup widgets are
// adapted: animations finish immediately and this fixture has no audio/video,
// resource archive, saved-player registry or native window. The AI never sees
// the authoritative state; it receives UserInterface's notification projection.
namespace
{
    monopoly::display::State displayState;
    monopoly::playerselection::State playerSelectionState;
    std::uint64_t presentationTick{};
    std::array<std::size_t, 256> observed{};
    std::array<std::size_t, 256> acceptedActions{};
    std::array<monopoly::actions::Message, 24> recent;
    std::size_t delivered{};
    bool initialStatePublished{};

    void require(bool value, std::string_view text)
    {
        if (!value) throw std::runtime_error(std::string(text));
    }
}

namespace monopoly::engine
{
    fonts::Runtime* fontPlayback() { return nullptr; }
    statsui::AccountRuntime* statsAccounts() noexcept { return nullptr; }
    bool startVoiceChat() noexcept { return false; }
    void stopVoiceChat() noexcept {}
    bool consumeOpeningMovieInput(const uimsg::Message&) { return false; }
    void playWarningSound() noexcept {}
    void playSaveFailureSound() noexcept {}
    void playClickSound() noexcept {}
    void playBuildSound() noexcept {}
    void playUnbuildSound() noexcept {}
    void playTokenVoice(std::uint8_t, udsound::TokenVoiceLine,
        udsound::TokenVoiceClipPolicy, bool) noexcept {}
    void playPennybagsVoice(udsound::PennybagsVoice,
        udsound::TokenVoiceClipPolicy, bool) noexcept {}
    bool spokenPostLockSlotEmpty() noexcept { return true; }
    bool isUsaBoardEdition() noexcept { return true; }
    void playJailChoiceHostComment() noexcept {}
}

namespace monopoly::startup
{
    std::shared_ptr<const data::ResourceSnapshot> resources() noexcept { return {}; }
}

namespace monopoly::display
{
    State& state() { return displayState; }
    const State& stateReadOnly() { return displayState; }
    void applyMusicTune(std::uint8_t value) noexcept { displayState.optionMusicTuneIndex = value; }
    void applyTokenVoicesOption(bool value) noexcept { displayState.optionTokenVoicesOn = value; }
    void applyHostCommentsOption(bool value) noexcept { displayState.optionHostCommentsOn = value; }
    void applyMusicOption(bool value) noexcept { displayState.optionMusicOn = value; }
    void applyRuntimeOptions(bool tokens, bool camera, bool lighting, bool board) noexcept
    {
        displayState.optionTokenAnimationsOn = tokens;
        displayState.optionCameraMovementOn = camera;
        displayState.optionLightingOn = lighting;
        displayState.game3DOn = board;
    }
    void setBackdrop(Screen2D value) { displayState.desired2DView = value; }
    void noteBoardActivity() noexcept {}
    void setTokenAnimationStackActive(bool) noexcept {}
    void requestBssmCamera(std::int32_t, std::uint8_t) noexcept {}
    void processBoardInput(const uimsg::Message&) {}
    void cycleIBarCamera(std::int32_t, bool) noexcept {}
}

namespace monopoly::chat
{
    void setPlayerNames(const rules::GameState&) {}
    void reset() noexcept {}
    bool processInput(const uimsg::Message&, rules::PlayerNumber, std::uint32_t, bool)
    { return false; }
    bool processRuleMessage(const actions::Message&) { return false; }
}

namespace monopoly::playerselection
{
    State& state() { return playerSelectionState; }
    const State& stateReadOnly() { return playerSelectionState; }
    bool consumeLoadRequest() noexcept { return false; }
    bool consumeCustomBoardRequest() noexcept { return false; }
    std::expected<void, std::string> commitCustomBoard(std::filesystem::path) { return {}; }
    void recordGameStarted() {}
    void processMessage(const actions::Message&) {}
    void processLibraryMessage(const uimsg::Message&) {}
    void playerButtonClicked(rules::PlayerNumber) {}
}

namespace monopoly::timers
{
    std::uint64_t tickCount() { return presentationTick; }
}

namespace monopoly::userinterface
{
    void advanceTimeStep() {}
    void lockGameQueue() {}
    void unlockGameQueue() {}
    bool gameQueueLocked() { return false; }
}

namespace
{
    using namespace monopoly;

    void deliver(const actions::Message& message, bool runAI = true)
    {
        recent[delivered++ % recent.size()] = message;
        ++observed[static_cast<std::size_t>(message.action)];
        if (message.action == actions::Type::NotifyActionCompleted && message.numberB == 1 &&
            message.numberA >= 0 && static_cast<std::size_t>(message.numberA) < acceptedActions.size())
            ++acceptedActions[static_cast<std::size_t>(message.numberA)];
        if ((message.toPlayer == rules::AllPlayers || message.toPlayer == rules::BankPlayer)
            && messaging::serverMode())
            rules::process(message);
        userinterface::processRuleMessage(message);
        userinterface::update();

        // End the presentation work that Engine would consume between frames.
        (void)userinterface::takePendingDiceRoll();
        (void)userinterface::takePendingPieceMovePlan();
        (void)userinterface::takePendingPieceMoveSpecial();
        (void)userinterface::takePendingPieceIdleTransitionPlan();
        if (message.action == actions::Type::NotifyAreYouThere &&
            message.numberC == static_cast<std::int64_t>(actions::Type::NotifyNewHighBid))
            require(userinterface::sendReadyResponses(
                static_cast<std::uint32_t>(message.numberA), message.numberB).has_value(),
                "auction presentation must acknowledge only the actual local players");

        if (message.action == actions::Type::NotifyClientResyncInfo &&
            message.binaryDataA.size() >= 7 &&
            message.binaryDataA[message.binaryDataA.size() - 7] == 1)
        {
            initialStatePublished = true;
            const auto& projected = userinterface::ruleStateReadOnly();
            for (rules::PlayerNumber player = 0; player < projected.numberOfPlayers; ++player)
                require(projected.players[player].cash == rules::state().options.initialCash &&
                    projected.players[player].currentSquare == 0,
                    "new-game resync must publish initial cash and GO to the real UI");
        }
        if (message.action == actions::Type::NotifyGameStarting)
            require(initialStatePublished, "initial client resync must precede GAME_STARTING");

        if (runAI) ai::processMessage(userinterface::ruleStateReadOnly(), message);
        require(userinterface::ruleStateReadOnly().numberOfPendingPhases == 0,
            "fixture must not inject private server phases into the UI or AI");
    }

    void drain(bool runAI = true)
    {
        actions::Message message;
        std::size_t count{};
        while (messaging::receiveAction(message))
        {
            require(++count < 10'000, "setup queue must drain without an action loop");
            deliver(message, runAI);
        }
    }

    void printDiagnostics()
    {
        const auto& state = rules::state();
        std::cerr << "phase=" << static_cast<int>(rules::phases::current(state).phase)
            << " player=" << static_cast<int>(state.currentPlayer)
            << " messages=" << delivered << " pending=" << messaging::currentQueueSize() << '\n';
        for (rules::PlayerNumber player = 0; player < state.numberOfPlayers; ++player)
            std::cerr << "player " << static_cast<int>(player) << " cash=" << state.players[player].cash
                << " uiCash=" << userinterface::ruleStateReadOnly().players[player].cash
                << " square=" << static_cast<int>(state.players[player].currentSquare)
                << " localAI=" << ui::localplayers::slotIsLocalAIPlayer(player) << '\n';
        const auto begin = delivered > recent.size() ? delivered - recent.size() : 0;
        for (auto i = begin; i < delivered; ++i)
        {
            const auto& m = recent[i % recent.size()];
            std::cerr << "message " << static_cast<int>(m.action) << " from=" << static_cast<int>(m.fromPlayer)
                << " a=" << m.numberA << " b=" << m.numberB << " c=" << m.numberC << '\n';
        }
    }

    void testComputerGame(rules::PlayerNumber playerCount = 2)
    {
        observed.fill(0);
        acceptedActions.fill(0);
        delivered = 0;
        initialStatePublished = false;
        require(messaging::initialize(), "local FIFO initializes");
        userinterface::resetRuleProjection();
        ui::localplayers::reset();
        ai::resetMessageIngress();
        require(ibar::initialize(), "real IBar initializes");
        require(ai::initializeMessageIngressProfiles(
            std::filesystem::path(MONOPOLY_LEGACY_SOURCE_DIR) / "monopoly").has_value(),
            "original checked-in computer profiles load");
        require(rules::initialize(), "production RULE initializes");
        rules::random::seed(12345);
        std::srand(54321);
        drain();
        constexpr std::array names{L"Computer One", L"Computer Two", L"Computer Three",
            L"Computer Four", L"Computer Five", L"Computer Six"};
        for (rules::PlayerNumber player = 0; player < playerCount; ++player)
        {
            require(ui::localplayers::requestAddLocalPlayer(userinterface::ruleStateReadOnly(),
                names[player], player, player, 2, false), "computer enters through real player setup");
            drain();
        }
        require(rules::state().numberOfPlayers == playerCount && ui::localplayers::count() == playerCount,
            "real naming notifications assign every local slot");
        require(messaging::sendAction(actions::Type::StartGame, 0, rules::BankPlayer), "start is queued");
        // All-AI games pre-approve the configuration. Queue the user's settings
        // before StartGame's ensuing RestartPhase can auto-start the game.
        auto options = rules::state().options;
        options.aiTakesTimeToThink = false;
        options.auctionGoingTimeDelay = 1;
        actions::Message accept;
        require(rules::configuration::acceptedConfigurationMessage(options, 0, false, accept),
            "real configuration encodes fast AI play");
        require(messaging::sendAction(accept), "configuration is queued");

        if (playerCount == rules::MaxPlayers)
        {
            // Six-player games can legitimately run indefinitely when the
            // deeds stay divided between opponents. Exercise every seat for
            // two complete rounds without imposing an invented turn limit.
            std::array<unsigned, rules::MaxPlayers> endedTurns{};
            std::size_t idleTicks{};
            while (std::any_of(endedTurns.begin(), endedTurns.end(),
                [](unsigned turns) { return turns < 2; }) &&
                delivered < 20'000 && idleTicks < 2'000)
            {
                actions::Message message;
                if (messaging::receiveAction(message))
                {
                    if (message.action == actions::Type::EndTurn &&
                        message.fromPlayer < playerCount)
                        ++endedTurns[message.fromPlayer];
                    deliver(message);
                }
                else
                {
                    ++idleTicks;
                    presentationTick += 60;
                    rules::serviceIdleTick();
                    actions::Message tick;
                    tick.action = actions::Type::Tick;
                    tick.fromPlayer = rules::BankPlayer;
                    tick.toPlayer = rules::AllPlayers;
                    deliver(tick);
                }
            }
            drain();
            require(std::all_of(endedTurns.begin(), endedTurns.end(),
                [](unsigned turns) { return turns >= 2; }) &&
                observed[static_cast<std::size_t>(actions::Type::EndTurn)] ==
                acceptedActions[static_cast<std::size_t>(actions::Type::EndTurn)],
                "all six real computer players complete repeated accepted turns");
            require(observed[static_cast<std::size_t>(actions::Type::BuyOrAuctionDecision)] != 0 &&
                runtime::state().gameInProgress,
                "full-table game continues through real property decisions");
            std::cout << "[PASS] all six AI seats complete repeated turns through the real UI projection\n";
            rules::shutdown();
            ibar::shutdown();
            messaging::shutdown();
            return;
        }

        // No scripted rolls, purchases, bids, trades or bankruptcy requests:
        // all decisions below come from the production AI and RULE messages.
        const auto playUntilGameOver = [](std::size_t expectedGames)
        {
            std::size_t idleTicks{};
            while (observed[static_cast<std::size_t>(actions::Type::NotifyGameOver)] < expectedGames &&
                   delivered < 150'000 && idleTicks < 10'000)
            {
                actions::Message message;
                if (messaging::receiveAction(message)) deliver(message);
                else
                {
                    ++idleTicks;
                    presentationTick += 60;
                    rules::serviceIdleTick();
                    actions::Message tick;
                    tick.action = actions::Type::Tick;
                    tick.fromPlayer = rules::BankPlayer;
                    tick.toPlayer = rules::AllPlayers;
                    deliver(tick);
                }
            }
            require(observed[static_cast<std::size_t>(actions::Type::NotifyGameOver)] == expectedGames,
                "computer game must finish within its bounded action budget");
        };
        playUntilGameOver(1);
        require(observed[static_cast<std::size_t>(actions::Type::NotifyGameOver)] != 0,
            "local AIs must reach game over through the real UI projection without stalling");
        require(observed[static_cast<std::size_t>(actions::Type::RollDice)] >= 4,
            "computer game includes repeated turns");
        require(observed[static_cast<std::size_t>(actions::Type::BuyOrAuctionDecision)] != 0,
            "computer game exercises property decisions");
        require(observed[static_cast<std::size_t>(actions::Type::GoBankrupt)] != 0,
            "computer game reaches bankruptcy without an injected game-over state");
        require(rules::phases::current(rules::state()).phase == rules::GamePhase::GameFinished,
            "RULE reaches its finished phase");
        require(!runtime::state().gameInProgress, "UI consumes game over");
        require(observed[static_cast<std::size_t>(actions::Type::GoBankrupt)] >= playerCount - 1u,
            "full game eliminates every player except the winner through actual bankruptcy actions");
        std::cout << "[PASS] complete " << static_cast<unsigned>(playerCount) << "-player AI game: "
            << delivered << " messages, "
            << observed[static_cast<std::size_t>(actions::Type::RollDice)] << " rolls, "
            << observed[static_cast<std::size_t>(actions::Type::BuyHouse)] << " house purchases\n";

        // Multiple queued RestartPhase actions may repeat the finished-state
        // notification. Deliver the entire previous game before requesting a
        // new one, rather than counting that repeat as the second game's end.
        drain();
        const auto previousGameOvers = observed[static_cast<std::size_t>(actions::Type::NotifyGameOver)];
        const auto previousRolls = observed[static_cast<std::size_t>(actions::Type::RollDice)];
        initialStatePublished = false;
        require(messaging::sendAction(actions::Type::NewGame, 0, rules::BankPlayer, 1),
            "play again queues the real NewGame action with retained players");
        // Do not reset AI, UI, ownership, profiles or the message queue here.
        // The real production notifications must retire the preceding game.
        playUntilGameOver(previousGameOvers + 1);
        require(initialStatePublished &&
            observed[static_cast<std::size_t>(actions::Type::NotifyGameStarting)] == 2 &&
            observed[static_cast<std::size_t>(actions::Type::RollDice)] >= previousRolls + 4,
            "the same local AIs start and finish a second game without fixture resets");
        std::cout << "[PASS] play again completes a second AI game without resetting the fixture\n";
        rules::shutdown();
        ibar::shutdown();
        messaging::shutdown();
    }

    void serviceHumanIdleTick()
    {
        // TimeStep services RULE after the FIFO is drained; WaitStartTurn
        // deliberately waits for this boundary before emitting its prompt.
        presentationTick += 60;
        actions::Message tick;
        tick.action = actions::Type::Tick;
        tick.fromPlayer = rules::BankPlayer;
        tick.toPlayer = rules::AllPlayers;
        deliver(tick);
        rules::serviceIdleTick();
        drain();
    }

    ibar::RuleActionHitState prepareHumanInput(ibar::RuleMode expectedMode)
    {
        const auto& projection = userinterface::iBarRuleStateReadOnly();
        const auto player = ibar::resolveRulePlayer(projection.player);
        const auto mode = ibar::resolveRuleMode(projection.mode, projection.player);
        require(mode == expectedMode && ui::localplayers::slotIsLocalHumanPlayer(player),
            "real notifications must expose the expected local human decision");
        // Reproduce Engine's presentation boundary using the production hit
        // planner. Neither the mode nor the active mask is invented by the test.
        userinterface::dicePromptState().show();
        ibar::ActionButtonInputs inputs;
        inputs.desired2DView = displayState.desired2DView;
        inputs.ruleMode = mode;
        inputs.rulePlayer = player;
        inputs.gameInProgress = runtime::state().gameInProgress;
        inputs.rollDiceDesired = userinterface::dicePromptState().currentStartTurn;
        inputs.raiseCashCanBankrupt = projection.raiseCashCanBankrupt;
        inputs.tradeEligible = ui::localplayers::tradeSourcePlayer(
            userinterface::ruleStateReadOnly(), player) < rules::MaxPlayers;
        const auto hit = ibar::ruleActionHitState(
            display::isIBarVisible(inputs.desired2DView), player, inputs);
        ibar::show();
        ibar::setRuleActionHitState(hit.layout, hit.activeSlots, mode, player, false);
        return hit;
    }

    void pressHumanMain(ibar::RuleMode expectedMode, actions::Type expectedAction, bool keyboard = false,
        ibar::layout::ActionButtonSlot slot = ibar::layout::ActionButtonSlot::Main)
    {
        const auto hit = prepareHumanInput(expectedMode);
        require((hit.activeSlots & ibar::layout::actionButtonBit(slot)) != 0,
            "production IBar planner exposes the requested human button");
        const auto index = static_cast<std::size_t>(expectedAction);
        const auto sentBefore = observed[index];
        const auto acceptedBefore = acceptedActions[index];
        if (keyboard)
            require(userinterface::processUIMessage({uimsg::Type::KeyboardPressed, SDL_SCANCODE_SPACE}),
                "real UI dispatch accepts the Space shortcut");
        else
        {
            const auto rect = ibar::layout::actionButtonRect(slot, hit.layout);
            require(userinterface::processUIMessage({uimsg::Type::MouseLeftDown,
                (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2}),
                "real UI dispatch accepts the IBar button click");
        }
        drain();
        serviceHumanIdleTick();
        require(observed[index] == sentBefore + 1 && acceptedActions[index] == acceptedBefore + 1,
            "human input emits exactly one action and RULE accepts it through the FIFO");
    }

    void loadHumanState(const rules::GameState& saved)
    {
        actions::Message load;
        load.action = actions::Type::SetGameState;
        load.fromPlayer = ui::localplayers::anyLocalPlayer(userinterface::ruleStateReadOnly());
        load.toPlayer = rules::BankPlayer;
        load.numberB = 1;
        load.numberC = 1;
        const rules::archive::AIStateArray aiStates{};
        require(rules::archive::encodeSave(saved, aiStates, load.binaryDataA),
            "human continuation fixture encodes a validated saved game");
        const auto acceptedBefore = acceptedActions[static_cast<std::size_t>(actions::Type::SetGameState)];
        require(messaging::sendAction(load), "human continuation loads through production RULE");
        drain();
        require(acceptedActions[static_cast<std::size_t>(actions::Type::SetGameState)] == acceptedBefore + 1,
            "RULE accepts the continuation archive from its actual local player");
    }

    void testHumanRejectedActions()
    {
        auto waiting = rules::state();
        waiting.currentPlayer = 0;
        waiting.numberOfPendingPhases = 1;
        waiting.phaseStack = {};
        waiting.phaseUndo = {};
        waiting.phaseStack[0].phase = rules::GamePhase::WaitMoveRoll;
        loadHumanState(waiting);
        require(messaging::sendAction(actions::Type::PlayerBuySellMort, 1, rules::BankPlayer),
            "another human interrupts the turn through the real property-management action");
        drain(false);
        require(rules::state().numberOfPendingPhases == 2 &&
            rules::phases::current(rules::state()).phase == rules::GamePhase::BuySellMortgage &&
            rules::phases::current(rules::state()).fromPlayer == 1,
            "property management owns the top phase while the first human's roll remains pending");

        const auto exchange = [](const actions::Message& request)
        {
            require(messaging::currentQueueSize() == 0 && messaging::sendAction(request),
                "rejected action enters an otherwise drained production FIFO");
            actions::Message queued;
            require(messaging::receiveAction(queued) && queued.action == request.action,
                "the actual queued request reaches RULE before its replies");
            deliver(queued, false);
            std::vector<actions::Message> replies;
            while (messaging::receiveAction(queued)) replies.push_back(std::move(queued));
            return replies;
        };
        const auto replay = [](const std::vector<actions::Message>& replies)
        {
            for (const auto& reply : replies) deliver(reply, false);
            drain(false);
        };
        const auto verifyRefusal = [](const std::vector<actions::Message>& replies,
            const actions::Message& request, rules::GamePhase phase, std::int64_t error,
            bool completedBeforeRefusal = false)
        {
            const auto offset = completedBeforeRefusal ? 1u : 0u;
            const bool internal = request.fromPlayer == rules::BankPlayer;
            require(replies.size() == offset + (internal ? 1u : 3u),
                "a stale action has exactly one completion, targeted error and restart; bank actions only complete");
            if (completedBeforeRefusal)
                require(replies[0].action == actions::Type::NotifyActionCompleted &&
                    replies[0].numberB == 1, "PauseGame preserves the retail completion before its phase check");
            const auto& completion = replies[offset];
            require(completion.action == actions::Type::NotifyActionCompleted &&
                completion.fromPlayer == rules::BankPlayer && completion.toPlayer == rules::AllPlayers &&
                completion.numberA == static_cast<std::int64_t>(request.action) &&
                completion.numberB == 0 && completion.numberC == request.fromPlayer && completion.numberD == 0,
                "refusal completion identifies the rejected action and player without copying its argument");
            if (internal) return;
            const auto& notification = replies[offset + 1];
            require(notification.action == actions::Type::NotifyErrorMessage &&
                notification.fromPlayer == rules::BankPlayer &&
                notification.toPlayer == (request.fromPlayer < rules::MaxPlayers
                    ? request.fromPlayer : rules::AllPlayers) &&
                notification.numberA == error &&
                notification.numberB == static_cast<std::int64_t>(request.action) &&
                notification.numberC == request.fromPlayer &&
                notification.numberD == static_cast<std::int64_t>(phase),
                "only the requester receives the source error with the phase at rejection time");
            const auto& restart = replies[offset + 2];
            require(restart.action == actions::Type::RestartPhase &&
                restart.fromPlayer == rules::BankPlayer && restart.toPlayer == rules::BankPlayer &&
                restart.numberA == 0 && restart.numberB == 0 && restart.numberC == 0 &&
                restart.numberD == 0 && restart.numberE == 0,
                "one clean bank restart follows the error instead of leaving the decision unrefreshed");
        };

        struct StaleAction
        {
            actions::Type action;
            std::int64_t error;
            std::int64_t argument{};
        };
        // Independent expected outcomes from the guards of these twelve
        // handlers in Source/monopoly/Rule.cpp, followed by ErrorWrong*.
        const std::array staleActions{
            StaleAction{actions::Type::RollDice, legacy_text::ErrorWrongPhase, 17},
            StaleAction{actions::Type::CardSeen, legacy_text::ErrorWrongPhase},
            StaleAction{actions::Type::ExitJailDecision, legacy_text::ErrorWrongPhase, 1},
            StaleAction{actions::Type::BuyOrAuctionDecision, legacy_text::ErrorWrongPhase, 1},
            StaleAction{actions::Type::TaxDecision, legacy_text::ErrorWrongPhase, 1},
            StaleAction{actions::Type::Mortgaging, legacy_text::ErrorWrongPlayer, 1},
            StaleAction{actions::Type::BuyHouse, legacy_text::ErrorWrongPlayer, 1},
            StaleAction{actions::Type::SellBuildings, legacy_text::ErrorWrongPlayer, 1},
            StaleAction{actions::Type::CancelDecomposition, legacy_text::ErrorWrongPhase},
            StaleAction{actions::Type::PlayerDoneBuySellMort, legacy_text::ErrorWrongPlayer},
            StaleAction{actions::Type::Bid, legacy_text::ErrorWrongPhase},
            StaleAction{actions::Type::StartHousingAuction, legacy_text::ErrorWrongPhase}};
        const auto interrupted = rules::state();
        for (const auto& stale : staleActions)
        {
            actions::Message request;
            request.action = stale.action;
            request.fromPlayer = 0;
            request.toPlayer = rules::BankPlayer;
            request.numberA = stale.argument;
            const auto prompts = observed[static_cast<std::size_t>(actions::Type::NotifyPlayerBuySellMort)];
            const auto replies = exchange(request);
            verifyRefusal(replies, request, rules::GamePhase::BuySellMortgage, stale.error);
            replay(replies);
            const auto& state = rules::state();
            require(state.numberOfPendingPhases == 2 && state.currentPlayer == 0 &&
                state.phaseStack[0].phase == rules::GamePhase::BuySellMortgage &&
                state.phaseStack[0].fromPlayer == 1 &&
                state.phaseStack[1].phase == rules::GamePhase::WaitMoveRoll &&
                state.players[0].cash == interrupted.players[0].cash &&
                state.players[0].currentSquare == interrupted.players[0].currentSquare &&
                state.squares[1].owner == interrupted.squares[1].owner &&
                state.squares[1].houses == interrupted.squares[1].houses &&
                state.squares[1].mortgaged == interrupted.squares[1].mortgaged,
                "a refused late action does not change the interrupted turn or its property");
            require(observed[static_cast<std::size_t>(actions::Type::NotifyPlayerBuySellMort)] == prompts + 1 &&
                userinterface::iBarRuleStateReadOnly().mode == ibar::RuleMode::OtherPlayer &&
                userinterface::iBarRuleStateReadOnly().player == 1,
                "the real restart republishes the other human's property decision into IBar");
        }

        loadHumanState(waiting);
        for (const auto requester : {rules::PlayerNumber{1}, rules::BankPlayer, rules::SpectatorPlayer})
        {
            actions::Message request;
            request.action = actions::Type::RollDice;
            request.fromPlayer = requester;
            request.toPlayer = rules::BankPlayer;
            request.numberA = 17;
            const auto prompts = observed[static_cast<std::size_t>(actions::Type::NotifyPleaseRollDice)];
            const auto replies = exchange(request);
            verifyRefusal(replies, request, rules::GamePhase::WaitMoveRoll, legacy_text::ErrorWrongPlayer);
            replay(replies);
            require(observed[static_cast<std::size_t>(actions::Type::NotifyPleaseRollDice)] ==
                    prompts + (requester == rules::BankPlayer ? 0u : 1u) &&
                userinterface::iBarRuleStateReadOnly().mode == ibar::RuleMode::StartTurn &&
                userinterface::iBarRuleStateReadOnly().player == 0,
                "a wrong player refreshes the actual roller; rejected bank work never creates a restart loop");
        }

        waiting.phaseStack[0].phase = rules::GamePhase::WaitEndTurn;
        loadHumanState(waiting);
        actions::Message pause;
        pause.action = actions::Type::PauseGame;
        pause.fromPlayer = 0;
        pause.toPlayer = rules::BankPlayer;
        pause.numberA = 1;
        const auto pauseReplies = exchange(pause);
        verifyRefusal(pauseReplies, pause, rules::GamePhase::WaitEndTurn, legacy_text::ErrorWrongPhase, true);
        replay(pauseReplies);
        require(rules::state().numberOfPendingPhases == 1 &&
            rules::phases::current(rules::state()).phase == rules::GamePhase::WaitEndTurn,
            "a refused end-turn pause leaves the phase stack intact");

        waiting.squares[1].owner = 1;
        waiting.squares[1].houses = 0;
        waiting.squares[1].mortgaged = false;
        loadHumanState(waiting);
        require(messaging::sendAction(actions::Type::PlayerBuySellMort, 0, rules::BankPlayer),
            "the active human opens property management for a semantic refusal");
        drain(false);
        actions::Message mortgage;
        mortgage.action = actions::Type::Mortgaging;
        mortgage.fromPlayer = 0;
        mortgage.toPlayer = rules::BankPlayer;
        mortgage.numberA = 1;
        mortgage.numberB = 1;
        const auto mortgageReplies = exchange(mortgage);
        require(mortgageReplies.size() == 2 &&
            mortgageReplies[0].action == actions::Type::NotifyActionCompleted && mortgageReplies[0].numberB == 0 &&
            mortgageReplies[1].action == actions::Type::NotifyErrorMessage &&
            mortgageReplies[1].numberA == legacy_text::ErrorMortgagingOnUnowned &&
            !rules::state().squares[1].mortgaged,
            "an unowned-property refusal retains its specific error without inventing a phase restart");
        replay(mortgageReplies);
        std::cout << "[PASS] twelve stale decisions, wrong players and refused pauses recover through RULE/FIFO/IBar\n";
    }

    void testHumanCounterOffer()
    {
        auto saved = rules::state();
        saved.currentPlayer = 0;
        saved.players[0].cash = saved.players[1].cash = 1000;
        saved.squares[1].owner = 0;
        saved.squares[1].mortgaged = false;
        saved.squares[1].houses = 0;
        saved.numberOfPendingPhases = 1;
        saved.phaseStack = {};
        saved.phaseStack[0].phase = rules::GamePhase::WaitEndTurn;
        loadHumanState(saved);

        const auto click = [](const auto& rect)
        {
            require(userinterface::processUIMessage({uimsg::Type::MouseLeftDown,
                (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2}),
                "trade click traverses the production UI dispatcher");
            drain();
        };
        const auto hit = prepareHumanInput(ibar::RuleMode::DoneTurn);
        require((hit.activeSlots & ibar::layout::actionButtonBit(ibar::layout::ActionButtonSlot::Trade)) != 0,
            "live human turn exposes the Trade button");
        click(ibar::layout::actionButtonRect(ibar::layout::ActionButtonSlot::Trade, hit.layout));
        require(displayState.desired2DView == display::Screen2D::Trade &&
            userinterface::tradeStateReadOnly().playerA == 0 && userinterface::tradeStateReadOnly().playerB == 1,
            "IBar opens a real two-human trade editor");

        const auto propertyLayout = tradeui::projectProperties(
            userinterface::tradeStateReadOnly(), userinterface::ruleStateReadOnly());
        click(propertyLayout.hitRects[0][1]);
        click(tradeui::CashTradeBT1);
        require(userinterface::tradeStateReadOnly().cashDialogVisible,
            "recipient opens the cash offer dialog");
        for (const char digit : std::string_view("1500"))
        {
            uimsg::Message text;
            text.type = uimsg::Type::TextInput;
            text.text.assign(1, digit);
            require(userinterface::processUIMessage(text), "cash digits traverse the real text-input route");
        }
        require(userinterface::processUIMessage({uimsg::Type::KeyboardPressed, SDL_SCANCODE_BACKSPACE}) &&
            userinterface::tradeStateReadOnly().cashTradeAmount == 150,
            "SDL Backspace edits the actual cash offer");
        require(userinterface::processUIMessage({uimsg::Type::KeyboardPressed, SDL_SCANCODE_RETURN}) &&
            !userinterface::tradeStateReadOnly().cashDialogVisible,
            "SDL Return accepts the cash offer");
        // Only the popup's closing animation is instantaneous in this fixture.
        userinterface::tradeState().cashDialogClosing = false;
        userinterface::tradeState().cashDialogFeedback = tradeui::CashDialogFeedback::None;

        const auto proposalsBefore = acceptedActions[static_cast<std::size_t>(actions::Type::StartTradeEditing)];
        click(tradeui::ProposeRect);
        require(acceptedActions[static_cast<std::size_t>(actions::Type::StartTradeEditing)] == proposalsBefore + 1 &&
            rules::phases::current(rules::state()).phase == rules::GamePhase::TradeAcceptance,
            "UI submits the property-for-cash offer and RULE asks for acceptance");
        pressHumanMain(ibar::RuleMode::Trading, actions::Type::TradeAccept, false,
            ibar::layout::ActionButtonSlot::General2);
        const auto& counter = userinterface::tradeStateReadOnly();
        require(counter.editMode && !counter.proposed && counter.showPropose &&
            counter.playerA == 1 && counter.playerB == 0 && counter.tradeFrom == 1,
            "Counter returns to a usable editor owned by the other human");

        click(tradeui::CashTradeAT1);
        require(userinterface::tradeStateReadOnly().cashOriginalOffers[0] == 150 &&
            userinterface::tradeStateReadOnly().cashOriginalOffers[1] == 0,
            "counter-offer cash dialog uses the new A/B sides without reversing payment");
        click(tradeui::Rect{129, 368, 179, 384}); // Cancel on the left cash popup.
        userinterface::tradeState().cashDialogClosing = false;
        userinterface::tradeState().cashDialogFeedback = tradeui::CashDialogFeedback::None;
        const auto& items = userinterface::tradeStateReadOnly().items;
        const auto cash = std::find_if(items.begin(), items.end(), [](const auto& item)
            { return item.numberC == static_cast<std::int64_t>(rules::TradeItemKind::Cash); });
        require(cash != items.end() && cash->numberA == 1 && cash->numberB == 0 && cash->numberD == 150,
            "cancelling the cash popup preserves the payer, recipient and amount");
        click(tradeui::ProposeRect);
        require(acceptedActions[static_cast<std::size_t>(actions::Type::StartTradeEditing)] == proposalsBefore + 2 &&
            rules::phases::current(rules::state()).phase == rules::GamePhase::TradeAcceptance,
            "the second human can submit the retained counter-offer to RULE");
        pressHumanMain(ibar::RuleMode::Trading, actions::Type::TradeAccept, false,
            ibar::layout::ActionButtonSlot::General3);
        require(rules::state().squares[1].owner == 1 &&
            rules::state().players[0].cash == 1150 && rules::state().players[1].cash == 850 &&
            userinterface::ruleStateReadOnly().squares[1].owner == 1 &&
            userinterface::ruleStateReadOnly().players[0].cash == 1150 &&
            userinterface::ruleStateReadOnly().players[1].cash == 850 &&
            !rules::state().tradeInProgress &&
            rules::phases::current(rules::state()).phase == rules::GamePhase::WaitEndTurn &&
            displayState.desired2DView == display::Screen2D::Main,
            "accepted counter-offer transfers the actual deed/cash and resumes the interrupted turn");
        std::cout << "[PASS] human property-for-cash counter-offer survives popup Cancel and completes through RULE\n";
    }

    void testHumanGameInput()
    {
        observed.fill(0);
        acceptedActions.fill(0);
        delivered = 0;
        initialStatePublished = false;
        require(messaging::initialize(), "human game FIFO initializes");
        userinterface::resetRuleProjection();
        ui::localplayers::reset();
        ai::resetMessageIngress();
        require(ibar::initialize() && rules::initialize(), "human IBar and RULE initialize");
        rules::random::seed(12345);
        drain();
        for (rules::PlayerNumber player = 0; player < 2; ++player)
        {
            require(ui::localplayers::requestAddLocalPlayer(userinterface::ruleStateReadOnly(),
                player == 0 ? L"Human One" : L"Human Two", player, player, 0, false),
                "human player enters through the production setup boundary");
            drain();
        }
        require(messaging::sendAction(actions::Type::StartGame, 0, rules::BankPlayer),
            "human game starts through the FIFO");
        for (rules::PlayerNumber player = 0; player < 2; ++player)
        {
            actions::Message accept;
            require(rules::configuration::acceptedConfigurationMessage(
                rules::state().options, player, false, accept) && messaging::sendAction(accept),
                "each human accepts the real game configuration");
        }
        drain();
        serviceHumanIdleTick();
        require(initialStatePublished && ui::localplayers::humanCount() == 2,
            "fresh human game publishes its initial state and local ownership");

        for (std::size_t step = 0; step < 64 &&
            observed[static_cast<std::size_t>(actions::Type::EndTurn)] < 4; ++step)
        {
            const auto mode = userinterface::iBarRuleStateReadOnly().mode;
            const bool keyboard = step % 2 != 0;
            switch (mode)
            {
            case ibar::RuleMode::StartTurn:
                pressHumanMain(mode, actions::Type::RollDice, keyboard); break;
            case ibar::RuleMode::DoneTurn:
                pressHumanMain(mode, actions::Type::EndTurn, keyboard); break;
            case ibar::RuleMode::BuyAuction:
                pressHumanMain(mode, actions::Type::BuyOrAuctionDecision, keyboard); break;
            case ibar::RuleMode::ViewingCard:
                pressHumanMain(mode, actions::Type::CardSeen, keyboard); break;
            case ibar::RuleMode::TaxDecision:
                pressHumanMain(mode, actions::Type::TaxDecision, keyboard); break;
            case ibar::RuleMode::JailExitPCR:
            case ibar::RuleMode::JailExitPXR:
                pressHumanMain(mode, actions::Type::ExitJailDecision, keyboard); break;
            default:
                throw std::runtime_error("human game stalled in IBar mode " +
                    std::to_string(static_cast<int>(mode)));
            }
        }
        require(observed[static_cast<std::size_t>(actions::Type::EndTurn)] == 4 &&
            observed[static_cast<std::size_t>(actions::Type::RollDice)] >= 4,
            "two humans complete repeated turns through real mouse/keyboard input");

        auto saved = rules::state();
        saved.currentPlayer = 0;
        saved.players[0].currentSquare = 2; // Community Chest.
        saved.numberOfPendingPhases = 2;
        saved.phaseStack = {};
        saved.phaseStack[0].phase = rules::GamePhase::WaitUntilCardSeen;
        saved.phaseStack[1].phase = rules::GamePhase::WaitEndTurn;
        loadHumanState(saved);
        const auto putAwayBefore = observed[static_cast<std::size_t>(actions::Type::NotifyPutAwayCard)];
        pressHumanMain(ibar::RuleMode::ViewingCard, actions::Type::CardSeen);
        require(observed[static_cast<std::size_t>(actions::Type::NotifyPutAwayCard)] == putAwayBefore + 1,
            "closing the real card panel applies the card and resumes its turn");

        saved = rules::state();
        saved.players[0].currentSquare = 0;
        saved.squares[1].owner = 0;
        saved.squares[1].mortgaged = true;
        saved.numberOfPendingPhases = 2;
        saved.phaseStack = {};
        saved.phaseStack[0].phase = rules::GamePhase::FreeUnmortgage;
        saved.phaseStack[0].toPlayer = 0;
        saved.phaseStack[0].amount = rules::board::propertyBit(rules::board::SquareType::MediterraneanAvenue);
        saved.phaseStack[1].phase = rules::GamePhase::WaitEndTurn;
        loadHumanState(saved);
        pressHumanMain(ibar::RuleMode::FreeUnmortgage, actions::Type::FreeUnmortgageDone, true);
        require(rules::phases::current(rules::state()).phase == rules::GamePhase::WaitEndTurn &&
            rules::state().squares[1].mortgaged,
            "Done closes the mortgage decision without changing the retained mortgage");
        std::cout << "[PASS] real human clicks/Space complete turns, dismiss cards and finish mortgage decisions\n";
        testHumanCounterOffer();
        testHumanRejectedActions();
        rules::shutdown();
        ibar::shutdown();
        messaging::shutdown();
    }

    void testHumanDebtChains()
    {
        observed.fill(0);
        acceptedActions.fill(0);
        delivered = 0;
        initialStatePublished = false;
        require(messaging::initialize(), "debt-chain FIFO initializes");
        userinterface::resetRuleProjection();
        ui::localplayers::reset();
        ai::resetMessageIngress();
        require(ibar::initialize() && rules::initialize(), "debt-chain IBar and RULE initialize");
        drain();
        constexpr std::array names{L"Debtor One", L"Debtor Two", L"Debtor Three", L"Debtor Four"};
        for (rules::PlayerNumber player = 0; player < names.size(); ++player)
        {
            require(ui::localplayers::requestAddLocalPlayer(userinterface::ruleStateReadOnly(),
                names[player], player, player, 0, false), "four humans register through real setup");
            drain();
        }
        require(messaging::sendAction(actions::Type::StartGame, 0, rules::BankPlayer),
            "debt-chain game starts through real setup");
        for (rules::PlayerNumber player = 0; player < names.size(); ++player)
        {
            actions::Message accept;
            require(rules::configuration::acceptedConfigurationMessage(
                rules::state().options, player, false, accept) && messaging::sendAction(accept),
                "all four humans accept their game options");
        }
        drain();
        serviceHumanIdleTick();
        const auto initial = rules::state();

        const auto cardState = [&](bool chairman, std::array<std::int64_t, 4> cash)
        {
            auto saved = initial;
            saved.currentPlayer = 0;
            for (rules::PlayerNumber player = 0; player < cash.size(); ++player)
            {
                saved.players[player].cash = cash[player];
                saved.players[player].currentSquare = 0;
            }
            saved.players[0].currentSquare = chairman ? 7 : 2;
            saved.players[0].firstMoveMade = true;
            for (auto& square : saved.squares)
            {
                square.owner = rules::BankPlayer;
                square.houses = 0;
                square.mortgaged = false;
            }
            saved.squares[39].owner = chairman ? 0 : 1;
            saved.squares[39].mortgaged = true;
            const auto deck = chairman ? rules::DeckType::Chance : rules::DeckType::Community;
            const auto card = chairman ? rules::CardType::ChancePay50ToEachPlayer :
                rules::CardType::CommunityGet50FromEachPlayer;
            auto& pile = saved.cards[static_cast<std::size_t>(deck)].cardPile;
            const auto found = std::find(pile.begin(), pile.end(), static_cast<std::uint8_t>(card));
            require(found != pile.end(), "card fixture retains the original legal deck");
            std::iter_swap(pile.begin(), found);
            saved.numberOfPendingPhases = 2;
            saved.phaseStack = {};
            saved.phaseUndo = {};
            saved.phaseStack[0].phase = rules::GamePhase::WaitUntilCardSeen;
            saved.phaseStack[1].phase = rules::GamePhase::WaitEndTurn;
            return saved;
        };
        const auto assertCash = [](std::array<std::int64_t, 4> expected)
        {
            for (rules::PlayerNumber player = 0; player < expected.size(); ++player)
                require(rules::state().players[player].cash == expected[player] &&
                    userinterface::ruleStateReadOnly().players[player].cash == expected[player],
                    "successive card payments agree in authoritative and projected cash balances");
        };

        loadHumanState(cardState(false, {100, 0, 100, 100}));
        pressHumanMain(ibar::RuleMode::ViewingCard, actions::Type::CardSeen);
        require(userinterface::iBarRuleStateReadOnly().player == 1,
            "birthday asks the next player for the first payment");
        pressHumanMain(ibar::RuleMode::RaiseMoney, actions::Type::GoBankrupt);
        require(userinterface::iBarRuleStateReadOnly().player == 0 &&
            rules::state().squares[39].owner == 0 && rules::state().players[1].currentSquare == 41,
            "first payer goes bankrupt to the card recipient before later payments");
        pressHumanMain(ibar::RuleMode::FreeUnmortgage, actions::Type::FreeUnmortgageDone);
        assertCash({180, 0, 50, 50});
        require(rules::state().squares[39].mortgaged &&
            rules::phases::current(rules::state()).phase == rules::GamePhase::WaitEndTurn,
            "mortgage fee and remaining birthday payments finish before the turn resumes");

        loadHumanState(cardState(true, {75, 100, 100, 100}));
        pressHumanMain(ibar::RuleMode::ViewingCard, actions::Type::CardSeen);
        assertCash({25, 150, 100, 100});
        require(userinterface::iBarRuleStateReadOnly().player == 0,
            "chairman pays the first creditor and must raise cash for the second");
        pressHumanMain(ibar::RuleMode::RaiseMoney, actions::Type::GoBankrupt);
        require(userinterface::iBarRuleStateReadOnly().player == 2 &&
            rules::state().squares[39].owner == 2 && rules::state().players[0].currentSquare == 41,
            "only the active chairman creditor receives the remaining estate");
        pressHumanMain(ibar::RuleMode::FreeUnmortgage, actions::Type::FreeUnmortgageDone, true);
        assertCash({0, 150, 105, 100});
        pressHumanMain(ibar::RuleMode::DoneTurn, actions::Type::EndTurn);
        require(rules::state().currentPlayer == 1,
            "bankrupt chairman can close the turn and the next surviving human plays");

        loadHumanState(cardState(false, {0, 0, 100, 100}));
        pressHumanMain(ibar::RuleMode::ViewingCard, actions::Type::CardSeen);
        pressHumanMain(ibar::RuleMode::RaiseMoney, actions::Type::GoBankrupt);
        require(userinterface::iBarRuleStateReadOnly().player == 0 &&
            userinterface::iBarRuleStateReadOnly().raiseCashNeeded == 20,
            "birthday recipient must pay interest while the inherited deed is held in escrow");
        pressHumanMain(ibar::RuleMode::RaiseMoney, actions::Type::GoBankrupt, true);
        assertCash({0, 0, 100, 100});
        require(rules::state().players[0].currentSquare == 41 &&
            rules::state().players[1].currentSquare == 41 &&
            rules::state().squares[39].owner == rules::BankPlayer &&
            !rules::state().squares[39].mortgaged,
            "cascade bankruptcy returns the escrow deed to the bank and cancels later collections");
        pressHumanMain(ibar::RuleMode::DoneTurn, actions::Type::EndTurn);
        require(rules::state().currentPlayer == 2,
            "the next turn skips both bankrupt players after the inherited-fee cascade");
        std::cout << "[PASS] human card debts, creditor order, mortgage escrow and cascading bankruptcy\n";
        rules::shutdown();
        ibar::shutdown();
        messaging::shutdown();
    }

    void testJailRollPrompt(bool doubles)
    {
        require(messaging::initialize(), "jail fixture FIFO initializes");
        userinterface::resetRuleProjection();
        ui::localplayers::reset();
        ai::resetMessageIngress();
        require(ibar::initialize(), "jail IBar initializes");
        require(rules::initialize(), "jail fixture RULE initializes");
        drain();
        rules::GameState saved;
        rules::options::setDefaults(saved.options);
        saved.options.cheatingAllowed = true;
        saved.numberOfPlayers = 2;
        saved.currentPlayer = 0;
        saved.players[0].name = L"Jailed human";
        saved.players[0].cash = 1000;
        saved.players[0].currentSquare = 40;
        saved.players[1].name = L"Other human";
        saved.players[1].cash = 1000;
        saved.players[1].token = 1;
        saved.players[1].colour = 1;
        saved.numberOfPendingPhases = 2;
        saved.phaseStack[0].phase = rules::GamePhase::JailRollOrPayOrCardDecision;
        saved.phaseStack[1].phase = rules::GamePhase::WaitEndTurn;
        actions::Message load;
        load.action = actions::Type::SetGameState;
        load.fromPlayer = rules::NobodyPlayer;
        load.toPlayer = rules::BankPlayer;
        load.numberB = 1;
        load.numberC = 1;
        const rules::archive::AIStateArray aiStates{};
        require(rules::archive::encodeSave(saved, aiStates, load.binaryDataA),
            "jail fixture is a validated saved game");
        require(messaging::sendAction(load), "saved jail turn enters real RULE dispatch");
        drain();
        const auto prompts = observed[static_cast<std::size_t>(actions::Type::NotifyPleaseRollDice)];
        require(messaging::sendAction(actions::Type::ExitJailDecision, 0, rules::BankPlayer, 0),
            "human requests a doubles attempt");
        drain();
        require(observed[static_cast<std::size_t>(actions::Type::NotifyPleaseRollDice)] > prompts &&
            rules::phases::current(rules::state()).phase == rules::GamePhase::WaitJailRoll,
            "choosing a jail roll emits the dice prompt through the actual restart dispatcher");
        const auto dice = observed[static_cast<std::size_t>(actions::Type::NotifyDiceRolled)];
        require(messaging::sendAction(actions::Type::CheatRollDice, 0, rules::BankPlayer,
            1, doubles ? 1 : 2), "fixed regression dice enter the documented cheat action");
        drain();
        require(observed[static_cast<std::size_t>(actions::Type::NotifyDiceRolled)] == dice + 1,
            "jail roll is accepted and projected");
        if (doubles)
            require(rules::state().players[0].currentSquare != 40,
                "doubles release the jailed player and continue the move");
        else
            require(rules::state().players[0].currentSquare == 40 &&
                rules::phases::current(rules::state()).phase == rules::GamePhase::WaitEndTurn,
                "failed doubles end the turn without leaving jail");
        std::cout << "[PASS] real jail prompt and " << (doubles ? "successful" : "failed") << " doubles\n";
        rules::shutdown();
        ibar::shutdown();
        messaging::shutdown();
    }

    struct LoopbackSockets
    {
        LoopbackSockets()
        {
#ifdef _WIN32
            WSADATA data{};
            require(WSAStartup(MAKEWORD(2, 2), &data) == 0, "initialize network fixture sockets");
#endif
        }
        ~LoopbackSockets()
        {
#ifdef _WIN32
            WSACleanup();
#endif
        }

        static std::uint16_t unusedPort()
        {
            const auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
            require(socket != INVALID_SOCKET, "create ephemeral-port probe");
#else
            require(socket >= 0, "create ephemeral-port probe");
#endif
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            const auto bound = ::bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
#ifdef _WIN32
            int length = sizeof(address);
#else
            socklen_t length = sizeof(address);
#endif
            const auto named = getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length);
#ifdef _WIN32
            closesocket(socket);
#else
            ::close(socket);
#endif
            require(bound == 0 && named == 0, "reserve an ephemeral loopback port");
            return ntohs(address.sin_port);
        }
    };

    void testNetworkGame()
    {
        // These are real TCP peers in one process. The host runs the production
        // RULE/UI/FIFO; peer input is scripted at the public transport boundary.
        LoopbackSockets sockets;
        observed.fill(0);
        acceptedActions.fill(0);
        delivered = 0;
        initialStatePublished = false;
        require(messaging::initialize(), "network fixture FIFO initializes");
        userinterface::resetRuleProjection();
        ui::localplayers::reset();
        ai::resetMessageIngress();
        require(ai::initializeMessageIngressProfiles(
            std::filesystem::path(MONOPOLY_LEGACY_SOURCE_DIR) / "monopoly").has_value(),
            "replacement AI profiles load");
        require(ibar::initialize() && rules::initialize(), "network IBar and RULE initialize");
        drain(false);
        require(ui::localplayers::requestAddLocalPlayer(
            userinterface::ruleStateReadOnly(), L"Host", 0, 0, 0, false), "host registers locally");
        drain(false);

        std::uint16_t port{};
        std::unique_ptr<messaging::Transport> host;
        for (unsigned attempt = 0; attempt < 8 && !host; ++attempt)
        {
            port = LoopbackSockets::unusedPort();
            auto opened = messaging::openTcpTransport(true, "127.0.0.1", port,
                messaging::TcpSessionMode::Gameplay);
            if (opened) host = std::move(*opened);
        }
        require(host && messaging::startNetwork(std::move(host)), "MESS owns the real gameplay listener");
        auto openPeer = [port]
        {
            auto opened = messaging::openTcpTransport(false, "127.0.0.1", port,
                messaging::TcpSessionMode::Gameplay);
            require(opened.has_value(), "open a real gameplay client");
            return std::move(*opened);
        };
        auto client = openPeer();
        auto observer = openPeer();
        std::vector<actions::Message> clientMessages, observerMessages;
        auto receive = [](messaging::Transport& peer, auto& messages)
        {
            peer.pump();
            actions::Message message;
            while (peer.receive(message)) messages.push_back(std::move(message));
        };
        auto pump = [&]
        {
            if (client) client->pump();
            observer->pump();
            drain();
            if (client) receive(*client, clientMessages);
            receive(*observer, observerMessages);
        };
        auto until = [&](auto done, std::string_view description)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!done())
            {
                require(std::chrono::steady_clock::now() < deadline, description);
                pump();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };
        auto count = [](const auto& messages, actions::Type type)
        {
            return std::count_if(messages.begin(), messages.end(),
                [type](const auto& message) { return message.action == type; });
        };
        until([&] { return count(clientMessages, actions::Type::NotifyClientResyncInfo) != 0 &&
            count(observerMessages, actions::Type::NotifyClientResyncInfo) != 0; },
            "both peers receive actual RULE admission snapshots");
        const auto remoteSource = client->localSourceId();
        require(remoteSource != 0 && remoteSource != observer->localSourceId(),
            "connections receive distinct host-assigned identities");
        actions::Message name;
        name.action = actions::Type::NamePlayer;
        name.fromPlayer = rules::SpectatorPlayer;
        name.toPlayer = rules::BankPlayer;
        name.numberA = rules::NobodyPlayer;
        name.numberC = name.numberD = 1;
        constexpr std::wstring_view remoteName = L"Remote";
        std::copy(remoteName.begin(), remoteName.end(), name.stringA.begin());
        require(client->send(name), "remote player registration crosses TCP");
        until([&] { return rules::state().numberOfPlayers == 2; }, "RULE registers the remote human");
        require(messaging::playerOwner(1) == remoteSource && messaging::playerOwner(0) == 0 &&
            ui::localplayers::humanCount() == 1 && !ui::localplayers::slotIsLocalPlayer(1),
            "registration preserves the distinction between host and remote ownership");

        // mt19937 seed 1 puts the second registered player first. This exercises
        // the ordinary random-order path, independently of the dice-order path.
        rules::random::seed(1);
        require(messaging::sendAction(actions::Type::StartGame, 0, rules::BankPlayer), "host starts the game");
        auto options = rules::state().options;
        options.cheatingAllowed = true; // Deterministic public dice action below.
        options.aiTakesTimeToThink = false;
        actions::Message accept;
        require(rules::configuration::acceptedConfigurationMessage(options, 0, false, accept) &&
            messaging::sendAction(accept), "host accepts game options");
        drain(false);
        serviceHumanIdleTick();
        require(rules::state().players[0].name == remoteName && rules::state().players[1].name == L"Host",
            "ordinary game start really changes the slot order");
        require(messaging::playerOwner(0) == remoteSource && messaging::playerOwner(1) == 0,
            "randomized player records retain their actual TCP owner");
        require(ui::localplayers::slotIsLocalHumanPlayer(1) && !ui::localplayers::slotIsLocalPlayer(0),
            "host UI follows reordered names without taking the remote slot");
        until([&] { return count(clientMessages, actions::Type::NotifyGameStarting) == 1 &&
            count(observerMessages, actions::Type::NotifyGameStarting) == 1; },
            "game start reaches both TCP peers");

        auto remoteAction = [](actions::Type action, std::int64_t a = 0, std::int64_t b = 0)
        {
            actions::Message message;
            message.action = action;
            message.fromPlayer = 0;
            message.toPlayer = rules::BankPlayer;
            message.numberA = a;
            message.numberB = b;
            return message;
        };
        const auto privateBefore = count(clientMessages, actions::Type::NotifyClientResyncInfo);
        const auto otherPrivateBefore = count(observerMessages, actions::Type::NotifyClientResyncInfo);
        require(client->send(remoteAction(actions::Type::ResyncClient, 0)),
            "owner requests a private client-state refresh");
        until([&] { return count(clientMessages, actions::Type::NotifyClientResyncInfo) > privateBefore; },
            "private refresh reaches its actual owner");
        require(count(observerMessages, actions::Type::NotifyClientResyncInfo) == otherPrivateBefore,
            "private client state is not delivered to another connection");

        const auto rollIndex = static_cast<std::size_t>(actions::Type::CheatRollDice);
        const auto rollsReceived = observed[rollIndex];
        const auto rollsAccepted = acceptedActions[rollIndex];
        require(observer->send(remoteAction(actions::Type::CheatRollDice, 1, 2)),
            "other peer submits an action claiming the remote player's slot");
        until([&] { return observed[rollIndex] > rollsReceived; }, "claimed action reaches host dispatch");
        require(acceptedActions[rollIndex] == rollsAccepted && rules::state().players[0].currentSquare == 0,
            "a different connection cannot move the active player's token");
        require(client->send(remoteAction(actions::Type::CheatRollDice, 1, 2)),
            "actual owner rolls through the same TCP action path");
        until([&] { return rules::phases::current(rules::state()).phase == rules::GamePhase::AuctionOrBuyDecision; },
            "remote roll reaches the property decision");
        require(rules::state().players[0].currentSquare == 3 && acceptedActions[rollIndex] == rollsAccepted + 1,
            "the reordered remote player can move legitimately");
        require(client->send(remoteAction(actions::Type::BuyOrAuctionDecision, 1)),
            "remote player buys the landed property");
        until([&] { return rules::state().squares[3].owner == 0 &&
            rules::phases::current(rules::state()).phase == rules::GamePhase::WaitEndTurn; },
            "remote purchase completes through authoritative RULE");
        until([&] { return std::any_of(observerMessages.begin(), observerMessages.end(), [](const auto& message)
            { return message.action == actions::Type::NotifySquareOwnership && message.numberA == 3 && message.numberB == 0; }); },
            "public ownership change reaches the other peer");
        require(rules::state().players[0].cash == options.initialCash - 60 &&
            userinterface::ruleStateReadOnly().players[0].cash == options.initialCash - 60,
            "host and authoritative state agree on the remote purchase");

        const auto endTurnsBefore = acceptedActions[static_cast<std::size_t>(actions::Type::EndTurn)];
        client.reset();
        until([&] { return rules::state().players[0].aiPlayerLevel == 3 && messaging::playerOwner(0) == 0; },
            "connection loss transfers the reordered remote slot to the host AI");
        require(ui::localplayers::slotIsLocalAIPlayer(0) && ui::localplayers::slotIsLocalHumanPlayer(1),
            "only the departed player's slot is taken over locally");
        serviceHumanIdleTick();
        require(acceptedActions[static_cast<std::size_t>(actions::Type::EndTurn)] > endTurnsBefore &&
            rules::state().currentPlayer == 1,
            "replacement AI completes the disconnected player's pending turn");
        std::cout << "[PASS] real TCP admission, shuffled ownership, private routing, remote purchase and AI takeover\n";
        observer.reset();
        rules::shutdown();
        ibar::shutdown();
        messaging::shutdown();
    }
}

int main()
{
    try
    {
        testJailRollPrompt(false);
        testJailRollPrompt(true);
        testHumanGameInput();
        testHumanDebtChains();
        testComputerGame();
        testComputerGame(rules::MaxPlayers);
        testNetworkGame();
    }
    catch (const std::exception& failure)
    {
        std::cerr << "[FAIL] " << failure.what() << '\n';
        printDiagnostics();
        return 1;
    }
    return 0;
}
