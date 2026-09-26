#include "AIMessageIngress.hpp"
#include "BoardRules.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"
#include "RuleBuildings.hpp"
#include "RuleEconomy.hpp"
#include "RuleOptions.hpp"
#include "RuleRandom.hpp"
#include "RuleTurnActions.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace monopoly::ui::localplayers
{
    // This fixture adapts local-player ownership only. Economic actions,
    // completions, phase restarts and turn actions use the production RULE code.
    bool isLocalRecipient(rules::PlayerNumber) { return true; }
    bool slotIsLocalAIPlayer(rules::PlayerNumber player) { return player == 0; }
    bool requestRemoveLocalPlayer(const rules::GameState&, rules::PlayerNumber)
    {
        throw std::runtime_error("retail AI profile failed to load");
    }
}

namespace
{
    using namespace monopoly;

    void require(bool condition, const char* description)
    {
        if (!condition) throw std::runtime_error(description);
        std::cout << "[PASS] " << description << '\n';
    }

    void deliverToAI(const rules::GameState& authoritative, const actions::Message& message)
    {
        // AI runs on public UI data, never on RULE's private pending phases.
        // The focused harness copies public fields directly; the full UI
        // projection and networking boundary are outside this test's scope.
        auto visible = authoritative;
        visible.numberOfPendingPhases = 0;
        visible.phaseStack = {};
        ai::processMessage(visible, message);
    }

    void testEconomicContinuation(bool houses, bool freeUnmortgage, bool debt = false)
    {
        require(messaging::initialize(), "economic RULE/AI fixture initializes FIFO");
        ai::resetMessageIngress();
        rules::buildings::resetTransientState();
        rules::economy::resetTransientState();
        rules::random::seed(12345);
        std::srand(12345);
        require(ai::initializeMessageIngressProfiles(
            std::filesystem::path(MONOPOLY_LEGACY_SOURCE_DIR) / "monopoly").has_value(),
            "economic RULE/AI fixture loads retail profiles");
        rules::GameState state{};
        rules::options::setDefaults(state.options);
        state.options.aiTakesTimeToThink = false;
        if (debt) state.options.maximumHouses = 0;
        require(rules::board::initializeForOptions(state.options), "board initializes");
        state.numberOfPlayers = 2;
        state.currentPlayer = 0;
        state.players[0].cash = debt ? 0 : 1000;
        state.players[0].aiPlayerLevel = 1;
        state.players[0].currentSquare = 0;
        state.players[1].cash = 1000;
        state.players[1].currentSquare = 20;
        rules::phases::push(state, houses && !debt ? rules::GamePhase::WaitEndTurn
                                        : rules::GamePhase::WaitMoveRoll,
            rules::NobodyPlayer, rules::NobodyPlayer, 0);
        const auto reading = rules::board::SquareType::ReadingRailroad;
        const auto mediterranean = rules::board::SquareType::MediterraneanAvenue;
        const auto baltic = rules::board::SquareType::BalticAvenue;
        if (houses)
        {
            state.squares[static_cast<std::size_t>(mediterranean)].owner = 0;
            state.squares[static_cast<std::size_t>(baltic)].owner = 0;
            if (debt)
            {
                state.squares[static_cast<std::size_t>(mediterranean)].houses = 5;
                state.squares[static_cast<std::size_t>(baltic)].houses = 5;
                rules::economy::stackDebt(state, 0, rules::BankPlayer, 100);
            }
        }
        else
        {
            state.squares[static_cast<std::size_t>(reading)].owner = 0;
            state.squares[static_cast<std::size_t>(reading)].mortgaged = true;
        }
        if (freeUnmortgage)
            rules::phases::push(state, rules::GamePhase::FreeUnmortgage,
                rules::BankPlayer, 0, rules::board::propertyBit(reading));
        actions::Message named{};
        named.action = actions::Type::NotifyNamePlayer;
        named.toPlayer = rules::AllPlayers;
        named.numberA = 0;
        deliverToAI(state, named);
        require(messaging::currentQueueSize() == 0, "profile load emits no gameplay action");
        require(messaging::sendAction(actions::Type::RestartPhase,
            rules::BankPlayer, rules::BankPlayer), "initial phase restart is queued");

        std::size_t delivered{};
        int purchases{};
        int sales{};
        int decompositionPrompts{};
        bool debtPaid{};
        int unmortgages{};
        int aiRestarts{};
        int releases{};
        int freeDone{};
        int turnActions{};
        bool resumed{};
        actions::Message message{};
        while (delivered++ < 512 && messaging::receiveAction(message))
        {
            switch (message.action)
            {
            case actions::Type::RestartPhase:
                if (message.fromPlayer == 0) ++aiRestarts;
                if (!rules::buildings::restartBuildingPhase(state) &&
                    !rules::economy::restartEconomyPhase(state, message))
                    require(rules::turnactions::restartGameplayPhase(state, message),
                        "queued restart has a production phase handler");
                break;
            case actions::Type::PlayerBuySellMort:
                rules::buildings::actionPlayerBuySellMortgage(state, message);
                break;
            case actions::Type::BuyHouse:
                ++purchases;
                require(rules::phases::current(state).phase == rules::GamePhase::BuySellMortgage,
                    "house action retains its actual RULE control");
                rules::buildings::actionBuyHouse(state, message);
                break;
            case actions::Type::SellBuildings:
                ++sales;
                rules::buildings::actionSellBuildings(state, message);
                break;
            case actions::Type::Mortgaging:
                ++unmortgages;
                rules::economy::actionMortgaging(state, message);
                break;
            case actions::Type::PlayerDoneBuySellMort:
                ++releases;
                rules::buildings::actionPlayerDoneBuySellMortgage(state, message);
                break;
            case actions::Type::FreeUnmortgageDone:
                ++freeDone;
                rules::economy::actionFreeUnmortgageDone(state, message);
                break;
            case actions::Type::EndTurn:
                ++turnActions;
                require(rules::phases::current(state).phase == rules::GamePhase::WaitEndTurn,
                    "end-turn action waits until economic control is released");
                rules::turnactions::actionEndTurn(state, message);
                resumed = state.currentPlayer == 1;
                break;
            case actions::Type::RollDice:
                ++turnActions;
                require(rules::phases::current(state).phase == rules::GamePhase::WaitMoveRoll,
                    "roll action waits until economic control is released");
                rules::turnactions::actionRollDice(state, message);
                resumed = rules::phases::current(state).phase == rules::GamePhase::MovingToken;
                break;
            default:
                if (message.action == actions::Type::NotifyDecomposeSale)
                    ++decompositionPrompts;
                if (message.action == actions::Type::NotifyActionCompleted &&
                    message.numberA == static_cast<std::int64_t>(actions::Type::NotifyPleasePay))
                    debtPaid = message.numberB != 0;
                deliverToAI(state, message);
                // Interleave ticks at the completion boundary: this previously
                // either stranded control or spent the underlying turn prompt.
                if (message.action == actions::Type::NotifyActionCompleted)
                {
                    require(message.numberB != 0,
                        "production RULE accepts every action in the economic exchange");
                    actions::Message tick{};
                    tick.action = actions::Type::Tick;
                    tick.fromPlayer = rules::BankPlayer;
                    tick.toPlayer = rules::AllPlayers;
                    deliverToAI(state, tick);
                }
                break;
            }
            if (resumed) break;
        }
        require(resumed && delivered < 512 && turnActions == 1,
            "economic exchange resumes the underlying turn exactly once without synthetic prompts");
        if (debt)
        {
            require(decompositionPrompts > 0 && sales == 10 && releases == 0 && debtPaid,
                "quick hotel liquidation finishes decomposition and pays debt without BSSM release");
            require(state.players[0].cash == 150,
                "ten real building sales pay the bank and leave the expected cash");
        }
        else if (houses)
        {
            const int total = state.squares[static_cast<std::size_t>(mediterranean)].houses +
                              state.squares[static_cast<std::size_t>(baltic)].houses;
            require(purchases >= 2 && total == purchases && aiRestarts == purchases,
                "each real house completion restarts and continues the spending loop");
            require(state.players[0].cash == 1000 - purchases * 50 && releases == 1,
                "RULE charges every purchase and AI releases exhausted control once");
        }
        else
        {
            require(unmortgages == 1 && aiRestarts == 1 &&
                    !state.squares[static_cast<std::size_t>(reading)].mortgaged,
                "real unmortgage completion queues exactly one AI restart");
            require(freeUnmortgage ? (freeDone == 0 && releases == 0)
                                  : (freeDone == 0 && releases == 1),
                "RULE closes the cleared free mortgage phase or AI releases its BSSM control");
        }
        ai::resetMessageIngress();
        messaging::shutdown();
    }
}

int main()
{
    try
    {
        testEconomicContinuation(true, false);
        testEconomicContinuation(false, false);
        testEconomicContinuation(false, true);
        testEconomicContinuation(true, false, true);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
