#include "AICounterTradeRuntime.hpp"
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

    void own(rules::GameState& state, SquareType square, rules::PlayerNumber player)
    {
        state.squares[static_cast<std::size_t>(square)].owner = player;
    }

    rules::GameState baseState()
    {
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.tradeInProgress = true;
        state.options.housesPerHotel = 5;
        state.options.evenBuildRule = true;
        state.players[0].cash = 500;
        state.players[1].cash = 500;
        return state;
    }

    ai::decision::CounterProposalPreflightConfig preflightConfig()
    {
        ai::decision::CounterProposalPreflightConfig config{};
        config.tradeCounterLimit = 3;
        config.numberTimesAllowPropertyTrade = 3;
        config.tradeCounterProbability = 1.0;
        config.evaluation.chancesThreshold = 1.0;
        config.evaluation.cashFactor = 1.0;
        config.playerAttitude[1] = 0.25;
        return config;
    }

    ai::decision::CounterProposalBalanceConfig balanceConfig()
    {
        ai::decision::CounterProposalBalanceConfig config{};
        config.fairTrade.evaluation.chancesThreshold = 1.0;
        config.fairTrade.cashMultipliers.fill(1.0);
        auto& importance = config.fairTrade.evaluation.propertyImportance;
        importance.monopolyReceivedImportance = 10.0;
        importance.propertyAllowTradeImportance = 10.0;
        importance.propertyTwoUnownedImportance = 10.0;
        importance.propertyOneUnownedImportance = 10.0;
        importance.railroadImportance = {10.0, 20.0, 30.0, 40.0};
        importance.utilityImportance = {10.0, 20.0};
        config.fairTrade.minEvaluationThreshold = 0.0;
        config.minEvaluationThreshold = 0.0;
        config.lowestPropertyImportanceForCounter = -100.0;
        config.maxGiveInTrade = 16;
        return config;
    }

    ai::trade::TradeProposalList propertyTrade()
    {
        ai::trade::TradeProposalList trade{};
        const auto mediterranean =
            rules::board::propertyBit(SquareType::MediterraneanAvenue);
        trade[0].propertiesGiven = mediterranean;
        trade[1].propertiesReceived = mediterranean;
        return trade;
    }

    ai::decision::CounterProposalPreflightInputs counterInputs(bool tradeAccept)
    {
        ai::decision::CounterProposalPreflightInputs inputs{};
        inputs.proposedPlayer = 1;
        inputs.tradeAccept = tradeAccept;
        inputs.counterRoll = 0.0;
        return inputs;
    }

    void testAcceptedTradeCounterCycle()
    {
        messaging::clearActionQueue();
        auto state = baseState();
        own(state, SquareType::MediterraneanAvenue, 0);
        const auto trade = propertyTrade();
        auto preflight = preflightConfig();
        auto balance = balanceConfig();
        auto inputs = counterInputs(true);
        ai::decision::CounterProposalSession session{};
        ai::trade::CounterTradeRuntimeState runtime{};

        const auto result = ai::trade::counterProposeTrade(
            state, 0, trade, inputs, preflight, balance, session, runtime);
        require(result.status == ai::trade::CounterTradeRunStatus::SendStarted &&
                result.preflightStatus == ai::decision::CounterProposalStatus::Ready &&
                result.balanceStatus == ai::decision::CounterProposalBalanceStatus::Ready &&
                session.timesCounteredTrade == 1 &&
                runtime.sending.state == ai::trade::SendingTradeState::AskingTradeEdit &&
                runtime.hasPendingProposal,
            "AI counter runtime stores balanced proposal and starts retail editor request");

        actions::Message message{};
        require(messaging::receiveAction(message) &&
                message.action == actions::Type::StartTradeEditing,
            "AI counter runtime emits start-edit action for received trade");
        ai::trade::processCounterProposalActionCompleted(
            runtime, actions::Type::StartTradeEditing, true);
        require(runtime.sending.state == ai::trade::SendingTradeState::TradeItems,
            "AI counter runtime advances to stored-item phase after editor grant");

        require(ai::trade::continueCounterProposalTradeSend(state, runtime) &&
                runtime.sending.state == ai::trade::SendingTradeState::TradeSent,
            "AI counter runtime sends stored proposal after trade-editor notification");
        require(messaging::currentQueueSize() == 3,
            "AI counter runtime queues balanced cash, property, and finalization");

        require(messaging::receiveAction(message) &&
                message.action == actions::Type::TradeItem &&
                message.numberA == 1 && message.numberB == 0 &&
                message.numberC == static_cast<std::int64_t>(rules::TradeItemKind::Cash) &&
                message.numberD == 17,
            "AI counter runtime sends retail normalized cash first");
        require(messaging::receiveAction(message) &&
                message.action == actions::Type::TradeItem &&
                message.numberA == 0 && message.numberB == 1 &&
                message.numberC == static_cast<std::int64_t>(rules::TradeItemKind::Square) &&
                message.numberD == static_cast<std::int64_t>(SquareType::MediterraneanAvenue),
            "AI counter runtime sends stored property after cash");
        require(messaging::receiveAction(message) &&
                message.action == actions::Type::TradeEditingDone &&
                message.numberA == 0 && message.numberB == 1,
            "AI counter runtime asks current trade participants to accept counter");

        ai::trade::processCounterProposalActionCompleted(
            runtime, actions::Type::TradeEditingDone, true);
        require(runtime.sending.state == ai::trade::SendingTradeState::Nothing &&
                !runtime.hasPendingProposal,
            "AI counter runtime clears stored proposal after trade finalization");
    }

    void testSuggestedTradeSendsDirectly()
    {
        messaging::clearActionQueue();
        auto state = baseState();
        own(state, SquareType::MediterraneanAvenue, 0);
        const auto trade = propertyTrade();
        auto preflight = preflightConfig();
        auto balance = balanceConfig();
        auto inputs = counterInputs(false);
        ai::decision::CounterProposalSession session{};
        ai::trade::CounterTradeRuntimeState runtime{};

        const auto result = ai::trade::counterProposeTrade(
            state, 0, trade, inputs, preflight, balance, session, runtime);
        require(result.status == ai::trade::CounterTradeRunStatus::SendStarted &&
                session.timesCounteredTrade == 1 &&
                runtime.sending.state == ai::trade::SendingTradeState::TradeSent &&
                runtime.sending.sendTradeOffer,
            "AI counter runtime sends suggestion directly from active editor");

        actions::Message message{};
        while (messaging::currentQueueSize() > 1)
            require(messaging::receiveAction(message),
                "AI counter runtime drains suggestion trade items before finalization");
        require(messaging::receiveAction(message) &&
                message.action == actions::Type::TradeEditingDone &&
                message.numberA == 1 && message.numberB == 1,
            "AI counter runtime preserves retail offer-trade finalization form");
    }

    void testNotInvolvedReturnsEditor()
    {
        messaging::clearActionQueue();
        auto state = baseState();
        ai::trade::TradeProposalList trade{};
        trade[1].cashGiven = 10;
        auto preflight = preflightConfig();
        auto balance = balanceConfig();
        auto inputs = counterInputs(false);
        ai::decision::CounterProposalSession session{};
        ai::trade::CounterTradeRuntimeState runtime{};

        const auto result = ai::trade::counterProposeTrade(
            state, 0, trade, inputs, preflight, balance, session, runtime);
        require(result.status == ai::trade::CounterTradeRunStatus::ReturnedNotInvolved &&
                result.preflightStatus == ai::decision::CounterProposalStatus::NotInvolved &&
                session.timesCounteredTrade == 0,
            "AI counter runtime returns editor when local AI is not involved");

        actions::Message message{};
        require(messaging::receiveAction(message) &&
                message.action == actions::Type::TradeEditingDone &&
                message.numberA == 1 && message.numberB == 1,
            "AI counter runtime queues retail return-to-proposer action");
    }

    void testPreflightStopAndQueueFailure()
    {
        messaging::clearActionQueue();
        auto state = baseState();
        own(state, SquareType::MediterraneanAvenue, 0);
        const auto trade = propertyTrade();
        auto preflight = preflightConfig();
        auto balance = balanceConfig();
        auto inputs = counterInputs(true);
        inputs.pendingActions = 1;
        ai::decision::CounterProposalSession session{};
        ai::trade::CounterTradeRuntimeState runtime{};

        auto result = ai::trade::counterProposeTrade(
            state, 0, trade, inputs, preflight, balance, session, runtime);
        require(result.status == ai::trade::CounterTradeRunStatus::PreflightStopped &&
                result.preflightStatus == ai::decision::CounterProposalStatus::InvalidTime &&
                messaging::currentQueueSize() == 0 && session.timesCounteredTrade == 0,
            "AI counter runtime stops before actions when retail preflight rejects time");

        messaging::clearActionQueue();
        actions::Message filler{};
        for (std::size_t index = 0; index < messaging::MessageQueueCapacity; ++index)
            require(messaging::sendAction(filler),
                "AI counter runtime fills message queue for failure fixture");

        inputs.pendingActions = 0;
        session = {};
        runtime = {};
        result = ai::trade::counterProposeTrade(
            state, 0, trade, inputs, preflight, balance, session, runtime);
        require(result.status == ai::trade::CounterTradeRunStatus::SendFailed &&
                session.timesCounteredTrade == 0 &&
                runtime.hasPendingProposal,
            "AI counter runtime does not increment retail counter when first send fails");
        messaging::clearActionQueue();
    }
}

int main()
{
    try
    {
        require(monopoly::rules::board::initializeForOptions(monopoly::rules::GameOptions{}),
            "AI counter runtime fixture initializes retail board definitions");
        require(monopoly::messaging::initialize(),
            "AI counter runtime fixture initializes messaging");
        testAcceptedTradeCounterCycle();
        testSuggestedTradeSendsDirectly();
        testNotInvolvedReturnsEditor();
        testPreflightStopAndQueueFailure();
        messaging::shutdown();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        messaging::shutdown();
        return 1;
    }
}
