#include "AIMessageIngress.hpp"
#include "AuctionUI.hpp"
#include "BoardRules.hpp"
#include "ChatRuntime.hpp"
#include "Display.hpp"
#include "Engine.hpp"
#include "ExtendedInitialization.hpp"
#include "IBar.hpp"
#include "IBarBackdropPlayback.hpp"
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
#include "TokenVoiceCatalog.hpp"
#include "UserInterface.hpp"

#include <SDL3/SDL_scancode.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

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

    void testComputerGame()
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
        require(ui::localplayers::requestAddLocalPlayer(
            userinterface::ruleStateReadOnly(), L"Computer One", 0, 0, 2, false), "first AI enters setup");
        drain();
        require(ui::localplayers::requestAddLocalPlayer(
            userinterface::ruleStateReadOnly(), L"Computer Two", 1, 1, 2, false), "second AI enters setup");
        drain();
        require(rules::state().numberOfPlayers == 2 && ui::localplayers::count() == 2,
            "real naming notifications assign both local slots");
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
            "two local AIs must reach game over through the real UI projection without stalling");
        require(observed[static_cast<std::size_t>(actions::Type::RollDice)] >= 4,
            "computer game includes repeated turns");
        require(observed[static_cast<std::size_t>(actions::Type::BuyOrAuctionDecision)] != 0,
            "computer game exercises property decisions");
        require(observed[static_cast<std::size_t>(actions::Type::GoBankrupt)] != 0,
            "computer game reaches bankruptcy without an injected game-over state");
        require(rules::phases::current(rules::state()).phase == rules::GamePhase::GameFinished,
            "RULE reaches its finished phase");
        require(!runtime::state().gameInProgress, "UI consumes game over");
        std::cout << "[PASS] complete AI game: " << delivered << " messages, "
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

    void pressHumanMain(ibar::RuleMode expectedMode, actions::Type expectedAction, bool keyboard = false)
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
        const auto hit = ibar::ruleActionHitState(
            display::isIBarVisible(inputs.desired2DView), player, inputs);
        require((hit.activeSlots & ibar::layout::actionButtonBit(ibar::layout::ActionButtonSlot::Main)) != 0,
            "production IBar planner exposes the human Main button");
        ibar::show();
        ibar::setRuleActionHitState(hit.layout, hit.activeSlots, mode, player, false);
        const auto index = static_cast<std::size_t>(expectedAction);
        const auto sentBefore = observed[index];
        const auto acceptedBefore = acceptedActions[index];
        if (keyboard)
            require(userinterface::processUIMessage({uimsg::Type::KeyboardPressed, SDL_SCANCODE_SPACE}),
                "real UI dispatch accepts the Space shortcut");
        else
        {
            const auto rect = ibar::layout::actionButtonRect(ibar::layout::ActionButtonSlot::Main, hit.layout);
            require(userinterface::processUIMessage({uimsg::Type::MouseLeftDown,
                (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2}),
                "real UI dispatch accepts the Main button click");
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
}

int main()
{
    try
    {
        testJailRollPrompt(false);
        testJailRollPrompt(true);
        testHumanGameInput();
        testComputerGame();
    }
    catch (const std::exception& failure)
    {
        std::cerr << "[FAIL] " << failure.what() << '\n';
        printDiagnostics();
        return 1;
    }
    return 0;
}
