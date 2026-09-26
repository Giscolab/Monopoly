#include "Actions.hpp"
#include "LocalPlayers.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"
#include "RuleArchive.hpp"
#include "RuleOptions.hpp"
#include "RuleRandom.hpp"
#include "RuleSave.hpp"
#include "RuleTurnActions.hpp"

#include <cstddef>
#include <iostream>
#include <string_view>

namespace
{
    using namespace monopoly;
    int failures{};

    void expect(bool condition, std::string_view description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << description << '\n';
        if (!condition) ++failures;
    }

    // This harness uses the production archive, FIFO, ownership consumer and
    // RULE phase/roll handlers. It adapts the small notification-delivery
    // boundary normally owned by UserInterface; it does not test that UI's
    // dispatch, rendering, compact resync decoder or mouse/keyboard routing.
    void testResumeFromEmptyLocalSession(bool allAI)
    {
        expect(messaging::initialize(), "local messaging initializes");
        ui::localplayers::reset();
        rules::save::resetTransientState();
        rules::random::seed(12345);

        rules::GameState saved{};
        rules::options::setDefaults(saved.options);
        saved.numberOfPlayers = 2;
        saved.currentPlayer = 1;
        saved.players[0].name = L"Saved computer";
        saved.players[0].aiPlayerLevel = 2;
        saved.players[0].currentSquare = 5;
        saved.players[0].cash = 1250;
        saved.players[1].name = L"Saved second player";
        saved.players[1].aiPlayerLevel = allAI ? 1 : 0;
        saved.players[1].currentSquare = 11;
        saved.players[1].cash = 987;
        saved.numberOfPendingPhases = 1;
        saved.phaseStack[0].phase = rules::GamePhase::WaitMoveRoll;

        rules::GameState ruleState{};
        rules::GameState uiState{};
        expect(ruleState.numberOfPlayers == 0 &&
            ui::localplayers::anyLocalPlayer(uiState) == rules::NobodyPlayer,
            "startup has neither RULE players nor a local sender");

        actions::Message load{};
        load.action = actions::Type::SetGameState;
        load.fromPlayer = rules::NobodyPlayer;
        load.toPlayer = rules::BankPlayer;
        load.numberB = 1;
        load.numberC = 1;
        const rules::archive::AIStateArray aiStates{};
        if (!rules::archive::encodeSave(saved, aiStates, load.binaryDataA))
        {
            expect(false, "real saved-game archive encodes");
            messaging::shutdown();
            return;
        }
        expect(messaging::sendAction(load), "startup load enters the production FIFO");

        bool loadCompleted{};
        bool resetSeen{};
        bool countSeen{};
        bool resyncSeen{};
        bool restartSeen{};
        bool promptSeen{};
        std::size_t added{};
        std::size_t named{};
        std::size_t delivered{};
        actions::Message message{};
        while (delivered < messaging::MessageQueueCapacity &&
               messaging::receiveAction(message))
        {
            ++delivered;
            if (message.action == actions::Type::SetGameState)
            {
                rules::save::actionSetGameState(ruleState, message);
                continue;
            }
            if (message.action == actions::Type::RestartPhase)
            {
                expect(resyncSeen && named == 2,
                    "restart follows ownership reconstruction and resync");
                expect(rules::turnactions::restartGameplayPhase(ruleState, message),
                    "the restored WaitMoveRoll phase is handled by production RULE");
                restartSeen = true;
                continue;
            }
            expect(ui::localplayers::isLocalRecipient(message.toPlayer),
                "saved-game notification reaches the local ownership consumer");
            if (!ui::localplayers::isLocalRecipient(message.toPlayer)) continue;

            if (message.action == actions::Type::NotifyActionCompleted &&
                message.numberA == static_cast<std::int64_t>(actions::Type::SetGameState))
                loadCompleted = message.numberB != 0;
            if (message.action == actions::Type::NotifyNumberOfPlayers)
            {
                if (message.numberA == 0)
                {
                    expect(added == 0 && named == 0, "reset precedes saved player announcements");
                    ui::localplayers::reset();
                    resetSeen = true;
                }
                else
                {
                    expect(resetSeen && added == 2, "saved local names precede the restored count");
                    countSeen = true;
                }
                uiState.numberOfPlayers = static_cast<rules::PlayerNumber>(message.numberA);
            }
            if (message.action == actions::Type::NotifyAddLocalPlayer)
            {
                expect(resetSeen && !countSeen, "local-name registration follows reset");
                ++added;
            }
            if (message.action == actions::Type::NotifyNamePlayer)
            {
                expect(countSeen && added == 2, "slot assignment follows registration of both names");
                ++named;
            }
            ui::localplayers::processRuleMessage(uiState, message);
            if (message.action == actions::Type::NotifyClientResyncInfo)
                resyncSeen = !message.binaryDataA.empty();
            if (message.action == actions::Type::NotifyPleaseRollDice)
            {
                expect(restartSeen && message.numberA == 1 && message.numberB == 11,
                    "resumed prompt identifies the saved player and square");
                ui::localplayers::setCurrentUIPlayerFromPlayerNumber(
                    static_cast<rules::PlayerNumber>(message.numberA));
                promptSeen = true;
            }
        }
        expect(messaging::currentQueueSize() == 0, "load and phase restart drain the bounded FIFO");
        expect(loadCompleted && promptSeen, "load from Nobody succeeds and emits a real roll prompt");
        expect(ruleState.players[1].cash == 987 && ruleState.players[1].currentSquare == 11,
            "the real archive restores the saved gameplay state");
        expect(ui::localplayers::count() == 2 &&
            ui::localplayers::slotIsLocalAIPlayer(0) &&
            ui::localplayers::slotIsLocalHumanPlayer(1),
            allAI ? "all-AI save promotes the last surviving AI before local slot assignment"
                  : "saved computer and human recover distinct local ownership");
        expect(ui::localplayers::currentUIPlayer() == 1 &&
            ui::localplayers::isLocalRecipient(1),
            "the prompted human has a selectable local slot");

        actions::Message roll{};
        roll.action = actions::Type::RollDice;
        roll.fromPlayer = ui::localplayers::currentUIPlayer();
        roll.toPlayer = rules::BankPlayer;
        expect(messaging::sendAction(roll) && messaging::receiveAction(message),
            "restored local human can queue a roll action");
        rules::turnactions::actionRollDice(ruleState, message);
        bool rollAccepted{};
        bool diceSeen{};
        while (messaging::receiveAction(message))
        {
            if (message.action == actions::Type::NotifyActionCompleted &&
                message.numberA == static_cast<std::int64_t>(actions::Type::RollDice))
                rollAccepted = message.numberB != 0 && message.numberC == 1;
            if (message.action == actions::Type::NotifyDiceRolled)
                diceSeen = message.numberA >= 1 && message.numberA <= 6 &&
                    message.numberB >= 1 && message.numberB <= 6 && message.numberC == 1;
        }
        expect(rollAccepted && diceSeen &&
            rules::phases::current(ruleState).phase == rules::GamePhase::MovingToken,
            "RULE accepts the restored human's roll and advances out of WaitMoveRoll");
        ui::localplayers::reset();
        messaging::shutdown();
    }
}

int main()
{
    testResumeFromEmptyLocalSession(false);
    testResumeFromEmptyLocalSession(true);
    return failures == 0 ? 0 : 1;
}
