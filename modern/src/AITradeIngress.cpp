#include "AITradeIngress.hpp"

namespace monopoly::ai::trade
{
    namespace
    {
        [[nodiscard]] bool decodePlayer(
            std::int64_t value,
            rules::PlayerNumber& player) noexcept
        {
            if (value >= 0 && value < rules::MaxPlayers)
            {
                player = static_cast<rules::PlayerNumber>(value);
                return true;
            }
            if (value == rules::NobodyPlayer)
            {
                player = rules::NobodyPlayer;
                return true;
            }
            return false;
        }

        void finishTrade(TradeIngressState& state) noexcept
        {
            state.currentTrade = {};
            state.immunities = {};
            state.counterRuntime = {};
            state.proposedPlayer = rules::NobodyPlayer;
            state.lastEditor = rules::NobodyPlayer;
            state.tradeStarted = false;
            state.tradeOfferedForAcceptance = false;
            state.tradeJustRejectedCountered = false;
            state.playerJustRejectedCountered = rules::NobodyPlayer;
            state.pendingTradeAcceptPlayers = 0;
            state.deferredAcceptancePlayers = 0;
            for (auto& session : state.counterSessions)
                session.timesCounteredTrade = 0;
        }
    }

    void resetTradeIngress(TradeIngressState& state) noexcept
    {
        state = {};
    }

    bool processTradeRuleMessage(
        const rules::GameState& gameState,
        const actions::Message& message,
        TradeIngressState& state) noexcept
    {
        switch (message.action)
        {
        case actions::Type::NotifyTradeStarted:
        {
            rules::PlayerNumber proposer = rules::NobodyPlayer;
            if (!decodePlayer(message.numberA, proposer) ||
                proposer == rules::NobodyPlayer)
                return false;

            state.currentTrade = {};
            state.immunities = {};
            const bool pendingOwnRequest = state.counterRuntime.hasPendingProposal &&
                state.counterRuntime.player == proposer &&
                (state.counterRuntime.sending.state == SendingTradeState::AskingTradeEdit ||
                 state.counterRuntime.sending.state == SendingTradeState::TradeItems);
            if (!pendingOwnRequest)
                state.counterRuntime = {};
            if (!state.tradeStarted)
            {
                state.proposedPlayer = proposer;
                state.lastEditor = proposer;
            }
            state.tradeStarted = true;
            state.tradeOfferedForAcceptance = false;
            state.tradeJustRejectedCountered = false;
            state.playerJustRejectedCountered = rules::NobodyPlayer;
            state.pendingTradeAcceptPlayers = 0;
            state.deferredAcceptancePlayers = 0;
            return true;
        }

        case actions::Type::NotifyTradeAcceptanceDecision:
            state.proposedPlayer = state.lastEditor;
            state.tradeOfferedForAcceptance = true;
            return true;

        case actions::Type::NotifyTradeItem:
        {
            rules::PlayerNumber from = rules::NobodyPlayer;
            rules::PlayerNumber to = rules::NobodyPlayer;
            if (!decodePlayer(message.numberA, from) ||
                !decodePlayer(message.numberB, to) ||
                message.numberC < 0 ||
                message.numberC > static_cast<std::int64_t>(
                    rules::TradeItemKind::FutureRent))
                return false;

            const auto kind = static_cast<rules::TradeItemKind>(message.numberC);
            const auto propertySet = static_cast<rules::board::PropertySet>(
                message.numberE);
            state.tradeStarted = true;
            return addTradeItem(
                state.currentTrade,
                state.immunities,
                kind,
                message.numberD,
                from,
                to,
                propertySet);
        }

        case actions::Type::NotifyTradeEditor:
        {
            if (state.tradeOfferedForAcceptance)
                state.tradeOfferedForAcceptance = false;

            rules::PlayerNumber editor = rules::NobodyPlayer;
            if (!decodePlayer(message.numberA, editor) ||
                editor == rules::NobodyPlayer)
                return false;

            if (state.lastEditor != editor)
            {
                state.proposedPlayer = state.lastEditor;
                state.lastEditor = editor;
            }
            state.tradeStarted = true;

            if (state.counterRuntime.hasPendingProposal &&
                state.counterRuntime.player == editor &&
                state.counterRuntime.sending.state == SendingTradeState::TradeItems)
            {
                (void)continueCounterProposalTradeSend(
                    gameState, state.counterRuntime);
            }
            return true;
        }

        case actions::Type::NotifyActionCompleted:
        {
            if (message.numberC < 0 || message.numberC >= rules::MaxPlayers)
                return false;

            const auto player = static_cast<rules::PlayerNumber>(message.numberC);
            const auto actionValue = message.numberA;
            if (actionValue == static_cast<std::int64_t>(actions::Type::TradeAccept))
            {
                state.pendingTradeAcceptPlayers &= ~(1u << player);
                if (state.tradeJustRejectedCountered &&
                    state.playerJustRejectedCountered == player)
                {
                    state.tradeJustRejectedCountered = false;
                    state.playerJustRejectedCountered = rules::NobodyPlayer;
                }
                return true;
            }

            if (actionValue != static_cast<std::int64_t>(actions::Type::StartTradeEditing) &&
                actionValue != static_cast<std::int64_t>(actions::Type::TradeEditingDone))
                return false;
            if (player != state.counterRuntime.player)
                return false;

            processCounterProposalActionCompleted(
                state.counterRuntime, static_cast<actions::Type>(actionValue),
                message.numberB != 0);
            if (actionValue == static_cast<std::int64_t>(actions::Type::StartTradeEditing) &&
                state.tradeJustRejectedCountered &&
                state.playerJustRejectedCountered == player)
            {
                state.tradeJustRejectedCountered = false;
                state.playerJustRejectedCountered = rules::NobodyPlayer;
            }
            return true;
        }

        case actions::Type::NotifyTradeFinished:
            finishTrade(state);
            return true;

        default:
            return false;
        }
    }

    bool sendProactiveTrade(
        const rules::GameState& gameState,
        rules::PlayerNumber player,
        const TradeProposalList& proposal,
        const profile::Profile& strategy,
        TradeIngressState& state) noexcept
    {
        if (gameState.numberOfPlayers > rules::MaxPlayers ||
            player >= gameState.numberOfPlayers ||
            state.counterRuntime.sending.state != SendingTradeState::Nothing ||
            !playerInvolvedInTrade(proposal[player]) ||
            !tradeIsProper(gameState, proposal))
            return false;
        const int spot = findFreeTradeSpot(
            state.turnState[player].timeLastTrade, strategy.maxTrades);
        if (spot < 0)
            return false;
        CounterTradeRuntimeState next{};
        next.player = player;
        next.proposedPlayer = player;
        next.pendingProposal = proposal;
        next.hasPendingProposal = true;
        next.sending.sendTradeOffer = true;
        if (!sendProposedTrade(gameState, player, proposal, false, player, next.sending))
            return false;
        state.counterRuntime = next;
        // Retail records the slot once the send starts, including an edit
        // request that RULE may subsequently deny.
        state.turnState[player].timeLastTrade[static_cast<std::size_t>(spot)] =
            strategy.timeForgetTrade;
        return true;
    }

    void advanceTradeTurn(
        std::uint8_t numberOfPlayers,
        profile::ProfileSet& profiles,
        const std::array<bool, rules::MaxPlayers>& localAIPlayers,
        TradeIngressState& state) noexcept
    {
        if (numberOfPlayers > rules::MaxPlayers)
            return;
        for (rules::PlayerNumber player = 0; player < numberOfPlayers; ++player)
        {
            if (!localAIPlayers[player])
                continue;
            auto& turn = state.turnState[player];
            auto& strategy = profiles[player];
            ++turn.turnsAfterForgettingLast;
            if (turn.turnsAfterForgettingLast >= strategy.turnsToForgetPropertyTrade)
            {
                forgetTradedProperties(state.counterSessions[player].propertyMemory);
                turn.turnsAfterForgettingLast = 0;
            }
            for (auto& timer : turn.timeLastTrade)
                if (timer > 0)
                    --timer;
            for (auto& attitude : strategy.playerAttitude)
            {
                if (attitude > strategy.neutralAttitude)
                    attitude -= strategy.attitudeTickChange;
                else
                    attitude += strategy.attitudeTickChange;
            }
        }
    }

    std::array<double, rules::MaxPlayers> tradeResponseAttitudeChanges(
        const rules::GameState& gameState,
        rules::PlayerNumber proposer,
        rules::PlayerNumber respondingPlayer,
        bool accepted,
        const TradeIngressState& state,
        const profile::ProfileSet& profiles,
        const profile::ConfigContext& context) noexcept
    {
        std::array<double, rules::MaxPlayers> changes{};
        if (gameState.numberOfPlayers > rules::MaxPlayers ||
            proposer >= rules::MaxPlayers ||
            respondingPlayer >= gameState.numberOfPlayers)
            return changes;

        if (!playerHasFutureOrImmunity(respondingPlayer, state.immunities))
        {
            for (rules::PlayerNumber observer = 0;
                 observer < gameState.numberOfPlayers; ++observer)
            {
                if (!context.localAIPlayer[observer])
                    continue;
                const auto& strategy = profiles[observer];
                double evaluation = decision::evaluateTrade(
                    gameState, respondingPlayer, observer, state.currentTrade,
                    profile::makeTradeEvaluationConfig(profiles, observer, context));
                evaluation *= strategy.attitudeChangeTradeObserving;
                if (evaluation < 0)
                {
                    if (evaluation < -strategy.attitudeLostForRejectedTrade)
                        evaluation = -strategy.attitudeLostForRejectedTrade;
                }
                else if (evaluation > strategy.attitudeLostForRejectedTrade)
                    evaluation = strategy.attitudeLostForRejectedTrade;

                if ((accepted && evaluation < 0) ||
                    (!accepted && evaluation > 0))
                    changes[observer] -= evaluation;
            }
        }
        // Retail applies this separately, even with futures/immunities.
        // It clamps the observed delta, not the resulting player attitude.
        if (context.localAIPlayer[proposer])
            changes[proposer] += accepted
                ? profiles[proposer].attitudeLostForRejectedTrade
                : -profiles[proposer].attitudeLostForRejectedTrade;
        return changes;
    }

    TradeAcceptanceResult evaluateCurrentTradeAcceptance(
        const rules::GameState& gameState,
        rules::PlayerNumber player,
        const TradeAcceptanceConfig& config,
        const TradeIngressState& state) noexcept
    {
        TradeAcceptanceResult result{};
        if (gameState.numberOfPlayers == 0 ||
            gameState.numberOfPlayers > rules::MaxPlayers ||
            player >= gameState.numberOfPlayers ||
            state.proposedPlayer >= gameState.numberOfPlayers)
            return result;

        if (state.tradeJustRejectedCountered)
        {
            result.status = TradeAcceptanceStatus::Suppressed;
            return result;
        }

        const auto playerBit = 1u << player;
        if ((state.pendingTradeAcceptPlayers & playerBit) != 0 ||
            (state.counterRuntime.player == player &&
             state.counterRuntime.sending.state != SendingTradeState::Nothing))
        {
            result.status = TradeAcceptanceStatus::Busy;
            return result;
        }

        if (!playerInvolvedInTrade(state.currentTrade[player]))
        {
            result.status = TradeAcceptanceStatus::AcceptUninvolved;
            return result;
        }

        if (playerHasFutureOrImmunity(player, state.immunities))
        {
            result.status = TradeAcceptanceStatus::RejectFutureOrImmunity;
            return result;
        }

        result.fedUp = propertiesAlreadyTraded(
            state.counterSessions[player].propertyMemory,
            state.proposedPlayer, state.currentTrade[player],
            config.numberTimesAllowPropertyTrade) != 0;
        result.evaluation = decision::evaluateTrade(
            gameState, player, player, state.currentTrade, config.evaluation);
        const double threshold = result.fedUp ?
            config.minEvaluationIfFedUp : config.minEvaluationThreshold;
        if (result.evaluation >= threshold)
        {
            result.status = TradeAcceptanceStatus::Accept;
            return result;
        }

        result.status = result.fedUp ?
            TradeAcceptanceStatus::RejectFedUp :
            TradeAcceptanceStatus::CounterOrReject;
        return result;
    }

    CounterTradeRunResult counterProposeCurrentTrade(
        const rules::GameState& gameState,
        rules::PlayerNumber player,
        bool tradeAccept,
        double counterRoll,
        int pendingActions,
        bool auctionOn,
        const decision::CounterProposalPreflightConfig& preflightConfig,
        const decision::CounterProposalBalanceConfig& balanceConfig,
        TradeIngressState& state) noexcept
    {
        CounterTradeRunResult result{};
        if (player >= gameState.numberOfPlayers ||
            state.proposedPlayer >= gameState.numberOfPlayers)
            return result;

        decision::CounterProposalPreflightInputs inputs{};
        inputs.proposedPlayer = state.proposedPlayer;
        inputs.pendingActions = pendingActions;
        inputs.tradeAccept = tradeAccept;
        inputs.counterRoll = counterRoll;
        inputs.auctionOn = auctionOn;
        inputs.playerSendingTrade =
            state.counterRuntime.sending.state != SendingTradeState::Nothing;

        return counterProposeTrade(
            gameState,
            player,
            state.currentTrade,
            inputs,
            preflightConfig,
            balanceConfig,
            state.counterSessions[player],
            state.counterRuntime,
            state.immunities);
    }
}
