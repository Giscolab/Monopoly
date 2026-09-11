#include "AIMessageIngress.hpp"
#include "AITradeIngress.hpp"
#include "BoardRules.hpp"
#include "Messaging.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;
    bool localRecipient = true;

    void require(bool condition, const char* description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << description << '\n';
        if (!condition) throw std::runtime_error(description);
    }

    rules::GameState baseState()
    {
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.tradeInProgress = true;
        state.players[0].cash = 500;
        state.players[1].cash = 500;
        return state;
    }
}

namespace monopoly::ui::localplayers
{
    bool isLocalRecipient(rules::PlayerNumber)
    {
        return localRecipient;
    }
}

namespace
{
    actions::Message editorMessage(rules::PlayerNumber editor)
    {
        actions::Message message{};
        message.action = actions::Type::NotifyTradeEditor;
        message.fromPlayer = rules::BankPlayer;
        message.toPlayer = rules::AllPlayers;
        message.numberA = editor;
        return message;
    }

    actions::Message tradeItemMessage(
        rules::PlayerNumber from,
        rules::PlayerNumber to,
        rules::TradeItemKind kind,
        std::int64_t value,
        rules::board::PropertySet properties = 0)
    {
        actions::Message message{};
        message.action = actions::Type::NotifyTradeItem;
        message.fromPlayer = rules::BankPlayer;
        message.toPlayer = rules::AllPlayers;
        message.numberA = from;
        message.numberB = to;
        message.numberC = static_cast<std::int64_t>(kind);
        message.numberD = value;
        message.numberE = static_cast<std::int64_t>(properties);
        return message;
    }

    void testTradeNotificationTracking()
    {
        auto state = baseState();
        ai::trade::TradeIngressState ingress{};
        ai::trade::resetTradeIngress(ingress);

        require(ai::trade::processTradeRuleMessage(
                    state, editorMessage(1), ingress) &&
                ingress.lastEditor == 1 &&
                ingress.proposedPlayer == rules::NobodyPlayer,
            "AI trade ingress tracks first editor without inventing proposer");

        const auto cash = tradeItemMessage(
            1, 0, rules::TradeItemKind::Cash, 40);
        require(ai::trade::processTradeRuleMessage(state, cash, ingress) &&
                ingress.currentTrade[1].cashGiven == 40 &&
                ingress.currentTrade[0].cashReceived == 40,
            "AI trade ingress mirrors retail cash notification");

        const auto mediterranean = rules::board::propertyBit(
            rules::board::SquareType::MediterraneanAvenue);
        const auto square = tradeItemMessage(
            1, 0, rules::TradeItemKind::Square,
            static_cast<std::int64_t>(
                rules::board::SquareType::MediterraneanAvenue));
        require(ai::trade::processTradeRuleMessage(state, square, ingress) &&
                (ingress.currentTrade[1].propertiesGiven & mediterranean) != 0 &&
                (ingress.currentTrade[0].propertiesReceived & mediterranean) != 0,
            "AI trade ingress mirrors retail square notification");

        require(ai::trade::processTradeRuleMessage(
                    state, editorMessage(0), ingress) &&
                ingress.proposedPlayer == 1 && ingress.lastEditor == 0,
            "AI trade ingress promotes previous editor to proposing player");

        auto invalid = tradeItemMessage(
            1, 0, rules::TradeItemKind::Cash, 5);
        invalid.numberC = 99;
        require(!ai::trade::processTradeRuleMessage(state, invalid, ingress),
            "AI trade ingress rejects invalid trade-item kind safely");
    }

    void testCounterSendContinuation()
    {
        auto state = baseState();
        ai::trade::TradeIngressState ingress{};
        ai::trade::resetTradeIngress(ingress);
        require(messaging::initialize(),
            "AI trade ingress fixture initializes messaging");
        messaging::clearActionQueue();

        ingress.counterRuntime.player = 0;
        ingress.counterRuntime.proposedPlayer = 1;
        ingress.counterRuntime.hasPendingProposal = true;
        ingress.counterRuntime.sending.state =
            ai::trade::SendingTradeState::AskingTradeEdit;
        ingress.counterRuntime.pendingProposal[0].cashGiven = 25;
        ingress.counterRuntime.pendingProposal[1].cashReceived = 25;

        actions::Message completed{};
        completed.action = actions::Type::NotifyActionCompleted;
        completed.numberA = static_cast<std::int64_t>(
            actions::Type::StartTradeEditing);
        completed.numberB = 1;
        completed.numberC = 0;
        require(ai::trade::processTradeRuleMessage(state, completed, ingress) &&
                ingress.counterRuntime.sending.state ==
                    ai::trade::SendingTradeState::TradeItems,
            "AI trade ingress advances granted editor request to trade items");

        require(ai::trade::processTradeRuleMessage(
                    state, editorMessage(0), ingress) &&
                ingress.counterRuntime.sending.state ==
                    ai::trade::SendingTradeState::TradeSent &&
                messaging::queuedActionCount() >= 2,
            "AI trade ingress sends stored counter after editor notification");

        actions::Message message{};
        bool sawCash = false;
        bool sawDone = false;
        while (messaging::receiveAction(message))
        {
            if (message.action == actions::Type::TradeItem &&
                message.numberC == static_cast<std::int64_t>(
                    rules::TradeItemKind::Cash))
                sawCash = true;
            if (message.action == actions::Type::TradeEditingDone)
                sawDone = true;
        }
        require(sawCash && sawDone,
            "AI trade ingress queues stored trade items and finalization");

        completed.numberA = static_cast<std::int64_t>(
            actions::Type::TradeEditingDone);
        require(ai::trade::processTradeRuleMessage(state, completed, ingress) &&
                ingress.counterRuntime.sending.state ==
                    ai::trade::SendingTradeState::Nothing &&
                !ingress.counterRuntime.hasPendingProposal,
            "AI trade ingress clears sending state after trade finalization");

        messaging::shutdown();
    }

    void testTradeFinishAndMessageFilter()
    {
        auto state = baseState();
        ai::trade::TradeIngressState ingress{};
        ingress.currentTrade[0].cashGiven = 5;
        ingress.counterSessions[0].timesCounteredTrade = 2;
        ingress.counterSessions[0].propertyMemory.bit1[1] = 1;
        ingress.lastEditor = 1;
        ingress.proposedPlayer = 0;

        actions::Message finished{};
        finished.action = actions::Type::NotifyTradeFinished;
        require(ai::trade::processTradeRuleMessage(state, finished, ingress) &&
                ingress.currentTrade[0].cashGiven == 0 &&
                ingress.counterSessions[0].timesCounteredTrade == 0 &&
                ingress.counterSessions[0].propertyMemory.bit1[1] == 1 &&
                ingress.lastEditor == rules::NobodyPlayer,
            "AI trade ingress finishes trade while preserving long-term property memory");

        ai::resetMessageIngress();
        localRecipient = false;
        ai::processMessage(state, editorMessage(1));
        require(ai::tradeIngressStateReadOnly().lastEditor ==
                    rules::NobodyPlayer,
            "AI message ingress ignores non-local recipients like retail");

        localRecipient = true;
        ai::processMessage(state, editorMessage(1));
        require(ai::tradeIngressStateReadOnly().lastEditor == 1,
            "AI message ingress forwards local trade notifications");
        ai::resetMessageIngress();
    }
}

int main()
{
    try
    {
        require(monopoly::rules::board::initializeForOptions(
                    monopoly::rules::GameOptions{}),
            "AI trade ingress fixture initializes board definitions");
        testTradeNotificationTracking();
        testCounterSendContinuation();
        testTradeFinishAndMessageFilter();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
