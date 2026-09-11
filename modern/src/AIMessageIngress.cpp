#include "AIMessageIngress.hpp"

#include "LocalPlayers.hpp"
#include "LegacyTextIds.hpp"
#include "Messaging.hpp"

#include <cstdlib>

namespace monopoly::ai
{
    namespace
    {
        trade::TradeIngressState tradeIngress{};
        profile::RuntimeState profileRuntime{};
        bool auctionOn{};

        void updateRuntimeFlags(const actions::Message& message) noexcept
        {
            if (message.action == actions::Type::NotifyNewHighBid)
                auctionOn = true;
            else if (message.action == actions::Type::NotifyAuctionGoing &&
                     message.numberD >= 3)
                auctionOn = false;
        }

        [[nodiscard]] profile::ConfigContext makeConfigContext() noexcept
        {
            profile::ConfigContext context{};
            for (rules::PlayerNumber player = 0;
                 player < rules::MaxPlayers; ++player)
            {
                context.localAIPlayer[player] =
                    ui::localplayers::slotIsLocalAIPlayer(player);
            }
            return context;
        }

        void processProfileMessage(
            const rules::GameState& state,
            const actions::Message& message) noexcept
        {
            if (message.action == actions::Type::NotifyPlayerDeleted)
            {
                if (message.numberA >= 0 &&
                    message.numberA < rules::MaxPlayers)
                {
                    profile::clearPlayerRuntime(
                        profileRuntime,
                        static_cast<rules::PlayerNumber>(message.numberA));
                }
                return;
            }

            if (message.action != actions::Type::NotifyNamePlayer ||
                message.numberA < 0 ||
                message.numberA >= rules::MaxPlayers)
                return;
            const auto player = static_cast<rules::PlayerNumber>(
                message.numberA);

            if (!ui::localplayers::slotIsLocalAIPlayer(player))
            {
                profile::clearPlayerRuntime(profileRuntime, player);
                return;
            }

            const auto& playerState = state.players[player];
            const auto loaded = profile::loadPlayerRuntime(
                profileRuntime,
                player,
                playerState.token,
                playerState.aiPlayerLevel);

            if (!loaded)
            {
                // Retail Userifce.cpp removes a local AI immediately when
                // AI_Load_AI() fails, then sends ACTION_NAME_PLAYER with an
                // empty name so RULE releases the slot.
                (void)ui::localplayers::requestRemoveLocalPlayer(
                    state, player);
            }
        }

        void processTradeAttitudeMessage(
            const rules::GameState& state,
            const actions::Message& message) noexcept
        {
            if (message.action == actions::Type::NotifyStartTurn)
            {
                auto context = makeConfigContext();
                for (rules::PlayerNumber player = 0; player < rules::MaxPlayers; ++player)
                    context.localAIPlayer[player] = context.localAIPlayer[player] &&
                        profileRuntime.playerLoaded[player];
                trade::advanceTradeTurn(state.numberOfPlayers, profileRuntime.players,
                    context.localAIPlayer, tradeIngress);
                return;
            }
            auto proposer = tradeIngress.proposedPlayer;
            std::int64_t respondingPlayer = -1;
            bool accepted = false;
            if (message.action == actions::Type::NotifyErrorMessage)
            {
                if (message.numberA == legacy_text::ErrorTradeChanging)
                {
                    tradeIngress.proposedPlayer = tradeIngress.lastEditor;
                    return;
                }
                if (message.numberA != legacy_text::ErrorTradeAccepted &&
                    message.numberA != legacy_text::ErrorTradeRejected)
                    return;
                accepted = message.numberA == legacy_text::ErrorTradeAccepted;
                respondingPlayer = message.numberC;
            }
            else if (message.action == actions::Type::NotifyTradeEditor &&
                     tradeIngress.tradeOfferedForAcceptance)
            {
                proposer = tradeIngress.lastEditor;
                respondingPlayer = message.numberA;
            }
            else
                return;

            if (respondingPlayer < 0 || respondingPlayer >= state.numberOfPlayers)
                return;
            auto context = makeConfigContext();
            for (rules::PlayerNumber player = 0; player < rules::MaxPlayers; ++player)
                context.localAIPlayer[player] = context.localAIPlayer[player] &&
                    profileRuntime.playerLoaded[player];
            const auto player = static_cast<rules::PlayerNumber>(respondingPlayer);
            const auto changes = trade::tradeResponseAttitudeChanges(
                state, proposer, player, accepted, tradeIngress,
                profileRuntime.players, context);
            for (rules::PlayerNumber observer = 0; observer < rules::MaxPlayers; ++observer)
                profileRuntime.players[observer].playerAttitude[player] += changes[observer];
        }

        [[nodiscard]] trade::TradeAcceptanceConfig makeAcceptanceConfig(
            rules::PlayerNumber player,
            const profile::ConfigContext& context) noexcept
        {
            trade::TradeAcceptanceConfig config{};
            config.evaluation = profile::makeTradeEvaluationConfig(
                profileRuntime.players, player, context);
            const auto& strategy = profileRuntime.players[player];
            config.numberTimesAllowPropertyTrade =
                strategy.numberTimesAllowPropertyTrade;
            config.minEvaluationThreshold = strategy.minEvaluationThreshold;
            config.minEvaluationIfFedUp = strategy.minEvaluationIfFedUp;
            return config;
        }

        [[nodiscard]] double retailCounterRoll() noexcept
        {
            return static_cast<double>(std::rand()) /
                static_cast<double>(RAND_MAX);
        }

        [[nodiscard]] bool sendTradeAcceptance(
            rules::PlayerNumber player, bool accept, std::int64_t status) noexcept
        {
            if (!messaging::sendAction(
                    actions::Type::TradeAccept, player, rules::BankPlayer,
                    accept ? 1 : 0, status))
                return false;
            tradeIngress.pendingTradeAcceptPlayers |= 1u << player;
            return true;
        }

        void maybeChooseTradeAcceptance(
            const rules::GameState& state,
            const actions::Message& message) noexcept
        {
            if (message.action != actions::Type::NotifyTradeAcceptanceDecision ||
                message.numberA < 0 || tradeIngress.tradeJustRejectedCountered)
                return;

            const auto requested = static_cast<std::uint32_t>(message.numberA);
            const auto context = makeConfigContext();
            for (rules::PlayerNumber player = 0;
                 player < state.numberOfPlayers; ++player)
            {
                const auto playerBit = 1u << player;
                if ((requested & playerBit) == 0 ||
                    !ui::localplayers::slotIsLocalAIPlayer(player) ||
                    !profileRuntime.playerLoaded[player])
                    continue;

                const auto config = makeAcceptanceConfig(player, context);
                const auto decision = trade::evaluateCurrentTradeAcceptance(
                    state, player, config, tradeIngress);
                switch (decision.status)
                {
                case trade::TradeAcceptanceStatus::Suppressed:
                case trade::TradeAcceptanceStatus::InvalidInput:
                    return;

                case trade::TradeAcceptanceStatus::Busy:
                    if ((tradeIngress.pendingTradeAcceptPlayers & playerBit) == 0)
                        tradeIngress.deferredAcceptancePlayers |= playerBit;
                    return;

                case trade::TradeAcceptanceStatus::AcceptUninvolved:
                    (void)sendTradeAcceptance(player, true, 3);
                    continue;

                case trade::TradeAcceptanceStatus::RejectFutureOrImmunity:
                case trade::TradeAcceptanceStatus::RejectFedUp:
                    (void)sendTradeAcceptance(player, false, 0);
                    return;

                case trade::TradeAcceptanceStatus::Accept:
                    (void)sendTradeAcceptance(player, true, 1);
                    continue;

                case trade::TradeAcceptanceStatus::CounterOrReject:
                    break;
                }

                tradeIngress.tradeJustRejectedCountered = true;
                tradeIngress.playerJustRejectedCountered = player;
                const auto preflight = profile::makeCounterPreflightConfig(
                    profileRuntime.players, player, context);
                const auto balance = profile::makeCounterBalanceConfig(
                    profileRuntime.players, player, context);
                const auto counter = trade::counterProposeCurrentTrade(
                    state, player, true,
                    tradeIngress.counterSessions[player].timesCounteredTrade == 0
                        ? retailCounterRoll() : 0.0,
                    0, auctionOn,
                    preflight, balance, tradeIngress);
                if (!counter.acted())
                    (void)sendTradeAcceptance(player, false, 0);
                return;
            }
        }

        void maybeRestartDeferredAcceptance(
            const actions::Message& message) noexcept
        {
            if (message.action != actions::Type::NotifyActionCompleted ||
                message.numberC < 0 || message.numberC >= rules::MaxPlayers)
                return;
            const auto player = static_cast<rules::PlayerNumber>(message.numberC);
            const auto bit = 1u << player;
            if ((tradeIngress.deferredAcceptancePlayers & bit) == 0)
                return;
            tradeIngress.deferredAcceptancePlayers &= ~bit;
            (void)messaging::sendAction(
                actions::Type::RestartPhase, player, rules::BankPlayer);
        }

        void maybeCounterTrade(
            const rules::GameState& state,
            const actions::Message& message) noexcept
        {
            if (message.action != actions::Type::NotifyTradeEditor ||
                message.numberA < 0 || message.numberA >= rules::MaxPlayers)
                return;

            const auto player = static_cast<rules::PlayerNumber>(message.numberA);
            if (!ui::localplayers::slotIsLocalAIPlayer(player) ||
                !profileRuntime.playerLoaded[player] ||
                tradeIngress.counterRuntime.sending.state !=
                    trade::SendingTradeState::Nothing)
                return;

            const auto context = makeConfigContext();
            const auto preflight = profile::makeCounterPreflightConfig(
                profileRuntime.players, player, context);
            const auto balance = profile::makeCounterBalanceConfig(
                profileRuntime.players, player, context);

            // With tradeAccept=false the retail probability roll is not read.
            const auto result = trade::counterProposeCurrentTrade(
                state, player, false, 0.0, 0, auctionOn,
                preflight, balance, tradeIngress);
            if (!result.acted())
            {
                (void)messaging::sendAction(
                    actions::Type::TradeEditingDone, player, rules::BankPlayer,
                    2, 0);
            }
        }
    }

    std::expected<void, profile::Error> initializeMessageIngressProfiles(
        const std::filesystem::path& directory)
    {
        return profile::initializeRuntime(profileRuntime, directory);
    }

    void resetMessageIngress() noexcept
    {
        trade::resetTradeIngress(tradeIngress);
        auctionOn = false;

        for (rules::PlayerNumber player = 0;
             player < rules::MaxPlayers;
             ++player)
        {
            profile::clearPlayerRuntime(profileRuntime, player);
        }
    }

    void processMessage(
        const rules::GameState& state,
        const actions::Message& message) noexcept
    {
        if (!ui::localplayers::isLocalRecipient(message.toPlayer))
            return;

        updateRuntimeFlags(message);
        processProfileMessage(state, message);
        processTradeAttitudeMessage(state, message);
        (void)trade::processTradeRuleMessage(
            state, message, tradeIngress);
        maybeRestartDeferredAcceptance(message);
        maybeChooseTradeAcceptance(state, message);
        maybeCounterTrade(state, message);
    }

    const trade::TradeIngressState&
        tradeIngressStateReadOnly() noexcept
    {
        return tradeIngress;
    }

    const profile::RuntimeState&
        profileRuntimeStateReadOnly() noexcept
    {
        return profileRuntime;
    }
}
