#include "AITradeSendRuntime.hpp"
#include "Messaging.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;
    using rules::board::SquareType;

    void require(bool condition, const char* description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << description << '\n';
        if (!condition) throw std::runtime_error(description);
    }

    ai::trade::TradeProposalList twoPlayerCashProposal()
    {
        ai::trade::TradeProposalList proposals{};
        proposals[0].cashReceived = 10;
        proposals[1].cashGiven = 10;
        return proposals;
    }

    void testRuntimeCycle()
    {
        require(messaging::initialize(), "AI trade-send runtime initializes messaging");
        rules::GameState state{};
        state.numberOfPlayers = 2;
        auto proposals = twoPlayerCashProposal();
        ai::trade::TradeSendRuntimeState runtime{};

        require(ai::trade::sendProposedTrade(
                    state, 0, proposals, false, rules::NobodyPlayer, runtime) &&
                runtime.state == ai::trade::SendingTradeState::AskingTradeEdit &&
                messaging::currentQueueSize() == 1,
            "AI trade-send runtime requests editing before sending items");
        actions::Message message{};
        require(messaging::receiveAction(message) &&
                message.action == actions::Type::StartTradeEditing &&
                message.fromPlayer == 0 && message.toPlayer == rules::BankPlayer,
            "AI trade-send runtime queues retail start-edit action");

        ai::trade::processTradeSendActionCompleted(
            runtime, actions::Type::StartTradeEditing, true);
        require(runtime.state == ai::trade::SendingTradeState::TradeItems,
            "AI trade-send runtime advances successful edit request to item phase");

        state.tradeInProgress = true;
        require(ai::trade::sendProposedTrade(
                    state, 0, proposals, false, rules::NobodyPlayer, runtime) &&
                runtime.state == ai::trade::SendingTradeState::TradeSent &&
                messaging::currentQueueSize() == 2,
            "AI trade-send runtime queues items and finalization after editor grant");
        require(messaging::receiveAction(message) &&
                message.action == actions::Type::TradeItem &&
                message.numberA == 1 && message.numberB == 0 &&
                message.numberC == static_cast<std::int64_t>(rules::TradeItemKind::Cash) &&
                message.numberD == 10,
            "AI trade-send runtime queues normalized cash transfer first");

        require(messaging::receiveAction(message) &&
                message.action == actions::Type::TradeEditingDone &&
                message.numberA == 0 && message.numberB == 1,
            "AI trade-send runtime submits completed trade for acceptance");

        runtime.sendTradeOffer = true;
        ai::trade::processTradeSendActionCompleted(
            runtime, actions::Type::TradeEditingDone, false);
        require(runtime.state == ai::trade::SendingTradeState::Nothing &&
                !runtime.sendTradeOffer,
            "AI trade-send runtime resets after editing-done completion regardless of result");

        runtime.state = ai::trade::SendingTradeState::AskingTradeEdit;
        runtime.sendTradeOffer = true;
        ai::trade::processTradeSendActionCompleted(
            runtime, actions::Type::StartTradeEditing, false);
        require(runtime.state == ai::trade::SendingTradeState::Nothing &&
                runtime.sendTradeOffer,
            "AI trade-send runtime preserves retail stale offer flag on failed edit request");
        messaging::shutdown();
    }
}

int main()
{
    try
    {
        testRuntimeCycle();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        messaging::shutdown();
        return 1;
    }
}
