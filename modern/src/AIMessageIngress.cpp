#include "AIMessageIngress.hpp"

#include "LocalPlayers.hpp"
#include "Messaging.hpp"

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
        (void)trade::processTradeRuleMessage(
            state, message, tradeIngress);
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
