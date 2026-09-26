#include "AIMessageIngress.hpp"
#include "AISaveState.hpp"

#include "LocalPlayers.hpp"
#include "LegacyTextIds.hpp"
#include "Messaging.hpp"
#include "RuleConfiguration.hpp"

#include <chrono>
#include <cstdlib>

namespace monopoly::ai
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        trade::TradeIngressState tradeIngress{};
        profile::RuntimeState profileRuntime{};
        bool auctionOn{};
        Clock::time_point gracePeriodForHumanActivity{};

        struct EconomicRuntimeState
        {
            rules::PlayerNumber controlOwner = rules::NobodyPlayer;
            bool decompositionControl{};
            std::array<decision::EconomicActionPlan, rules::MaxPlayers> pendingControlAction{};
            std::array<bool, rules::MaxPlayers> hasPendingControlAction{};
            std::array<bool, rules::MaxPlayers> controlRequestInFlight{};
            std::array<bool, rules::MaxPlayers> actionInFlight{};
            std::array<bool, rules::MaxPlayers> controlRestartInFlight{};
            std::array<bool, rules::MaxPlayers> controlReleaseInFlight{};
        };

        EconomicRuntimeState economicRuntime{};
        std::array<bool, rules::MaxPlayers> jailDecisionInFlight{};
        std::array<bool, rules::MaxPlayers> taxDecisionInFlight{};
        std::array<bool, rules::MaxPlayers> buyDecisionInFlight{};
        bool auctionBidInFlight{};
        rules::PlayerNumber auctionBiddingPlayer = rules::NobodyPlayer;
        std::array<bool, rules::MaxPlayers> housingAuctionRequestInFlight{};
        std::array<bool, rules::MaxPlayers> buildingPlacementInFlight{};
        std::array<bool, rules::MaxPlayers> debtActionInFlight{};
        std::array<std::int64_t, rules::MaxPlayers> debtMoneyOwed{};
        std::array<bool, rules::MaxPlayers> freeUnmortgageInFlight{};
        std::array<bool, rules::MaxPlayers> freeUnmortgageDoneInFlight{};
        enum class PendingTurnPrompt : std::uint8_t { None = 0, RollDice, EndTurn };
        std::array<PendingTurnPrompt, rules::MaxPlayers> pendingTurnPrompt{};
        std::array<bool, rules::MaxPlayers> turnActionInFlight{};
        std::array<bool, rules::MaxPlayers> turnPromptPaused{};
        std::array<rules::board::SquareType, rules::MaxPlayers> lastBuildingAction{};
        bool housingShortageOn{};
        bool buildingPlacementOn{};
        std::array<bool, rules::MaxPlayers> configurationAcceptInFlight{};
        std::array<bool, rules::MaxPlayers> cardSeenInFlight{};
        rules::PlayerNumber purchasingPlayer = rules::NobodyPlayer;
        rules::board::SquareType purchasingProperty = rules::board::SquareType::Count;

        [[nodiscard]] int pendingActionsForPlayer(rules::PlayerNumber player) noexcept
        {
            if (player >= rules::MaxPlayers) return 0;
            // Ai.cpp uses num_actions != 0 for trade acceptance/counter preflight.
            // Modern action owners keep separate flags, including a queued
            // economic control action. Count their busy states for that predicate;
            // this is not an attempt to reconstruct the retail message counter.
            return static_cast<int>(economicRuntime.hasPendingControlAction[player]) +
                economicRuntime.controlRequestInFlight[player] +
                economicRuntime.actionInFlight[player] +
                economicRuntime.controlRestartInFlight[player] +
                economicRuntime.controlReleaseInFlight[player] +
                jailDecisionInFlight[player] + taxDecisionInFlight[player] +
                buyDecisionInFlight[player] +
                (auctionBidInFlight && auctionBiddingPlayer == player) +
                housingAuctionRequestInFlight[player] + buildingPlacementInFlight[player] +
                debtActionInFlight[player] + freeUnmortgageInFlight[player] +
                freeUnmortgageDoneInFlight[player] + turnActionInFlight[player] +
                configurationAcceptInFlight[player] + cardSeenInFlight[player] +
                ((tradeIngress.pendingTradeAcceptPlayers & (1u << player)) != 0) +
                (tradeIngress.counterRuntime.player == player &&
                 tradeIngress.counterRuntime.sending.state != trade::SendingTradeState::Nothing);
        }

        void clearPlayerTransientState(
            rules::PlayerNumber player) noexcept
        {
            if (player >= rules::MaxPlayers)
                return;

            economicRuntime.pendingControlAction[player] = {};
            economicRuntime.hasPendingControlAction[player] = false;
            economicRuntime.controlRequestInFlight[player] = false;
            economicRuntime.actionInFlight[player] = false;
            economicRuntime.controlRestartInFlight[player] = false;
            economicRuntime.controlReleaseInFlight[player] = false;
            jailDecisionInFlight[player] = false;
            taxDecisionInFlight[player] = false;
            buyDecisionInFlight[player] = false;
            housingAuctionRequestInFlight[player] = false;
            buildingPlacementInFlight[player] = false;
            debtActionInFlight[player] = false;
            debtMoneyOwed[player] = 0;
            freeUnmortgageInFlight[player] = false;
            freeUnmortgageDoneInFlight[player] = false;
            pendingTurnPrompt[player] = PendingTurnPrompt::None;
            turnActionInFlight[player] = false;
            turnPromptPaused[player] = false;
            lastBuildingAction[player] = rules::board::SquareType::Count;
            if (economicRuntime.controlOwner == player)
            {
                economicRuntime.controlOwner = rules::NobodyPlayer;
                economicRuntime.decompositionControl = false;
            }
            configurationAcceptInFlight[player] = false;
            cardSeenInFlight[player] = false;

            const auto bit = 1u << player;
            tradeIngress.pendingTradeAcceptPlayers &= ~bit;
            tradeIngress.deferredAcceptancePlayers &= ~bit;
            tradeIngress.counterSessions[player] = {};
            tradeIngress.turnState[player] = {};
            if (tradeIngress.counterRuntime.player == player ||
                tradeIngress.counterRuntime.proposedPlayer == player)
                tradeIngress.counterRuntime = {};
            if (tradeIngress.playerJustRejectedCountered == player)
            {
                tradeIngress.tradeJustRejectedCountered = false;
                tradeIngress.playerJustRejectedCountered = rules::NobodyPlayer;
            }
            if (purchasingPlayer == player)
            {
                purchasingPlayer = rules::NobodyPlayer;
                purchasingProperty = rules::board::SquareType::Count;
            }
            if (auctionBiddingPlayer == player)
            {
                auctionBidInFlight = false;
                auctionBiddingPlayer = rules::NobodyPlayer;
            }
        }

        void resetGlobalTransientStateForResync() noexcept
        {
            auctionBidInFlight = false;
            auctionBiddingPlayer = rules::NobodyPlayer;
            auctionOn = false;
            housingShortageOn = false;
            buildingPlacementOn = false;
            economicRuntime.controlOwner = rules::NobodyPlayer;
            economicRuntime.decompositionControl = false;
            economicRuntime.controlRestartInFlight.fill(false);
            turnPromptPaused.fill(true);
            purchasingPlayer = rules::NobodyPlayer;
            purchasingProperty = rules::board::SquareType::Count;

            tradeIngress.tradeJustRejectedCountered = false;
            tradeIngress.playerJustRejectedCountered = rules::NobodyPlayer;
            tradeIngress.proposedPlayer = rules::NobodyPlayer;
            tradeIngress.lastEditor = rules::NobodyPlayer;
        }

        void updateRuntimeFlags(const actions::Message& message) noexcept
        {
            // A quick debt sale can enter decomposition without an underlying
            // BSSM phase. Its next decision notification is the only public
            // evidence that this temporary control has ended.
            if (economicRuntime.decompositionControl &&
                (message.action == actions::Type::NotifyPleasePay ||
                 message.action == actions::Type::NotifyPleaseRollDice ||
                 message.action == actions::Type::NotifyEndTurn ||
                 message.action == actions::Type::NotifyFreeUnmortgaging ||
                 message.action == actions::Type::NotifyPlaceBuilding ||
                 message.action == actions::Type::NotifyJailExitChoice ||
                 message.action == actions::Type::NotifyFlatOrFractionTaxDecision ||
                 message.action == actions::Type::NotifyBuyOrAuctionDecision ||
                 message.action == actions::Type::NotifyPickedUpCard ||
                 (message.action == actions::Type::NotifyActionCompleted &&
                  message.numberA == static_cast<std::int64_t>(actions::Type::NotifyPleasePay))))
            {
                if (economicRuntime.controlOwner < rules::MaxPlayers)
                    economicRuntime.controlRestartInFlight[economicRuntime.controlOwner] = false;
                economicRuntime.controlOwner = rules::NobodyPlayer;
                economicRuntime.decompositionControl = false;
            }
            // UI clients have no authoritative phase stack. Like Ai.cpp, track
            // economic ownership and modal decisions from RULE notifications.
            if ((message.action == actions::Type::NotifyPlayerBuySellMort ||
                 message.action == actions::Type::NotifyDecomposeSale) &&
                message.numberA >= 0 &&
                (message.numberA < rules::MaxPlayers || message.numberA == rules::NobodyPlayer))
            {
                economicRuntime.controlOwner =
                    static_cast<rules::PlayerNumber>(message.numberA);
                economicRuntime.decompositionControl =
                    message.action == actions::Type::NotifyDecomposeSale;
                if (economicRuntime.controlOwner < rules::MaxPlayers)
                {
                    economicRuntime.controlRestartInFlight[economicRuntime.controlOwner] = false;
                    if (message.action == actions::Type::NotifyPlayerBuySellMort)
                        debtMoneyOwed[economicRuntime.controlOwner] =
                            message.numberC > 0 ? message.numberC : 0;
                    turnPromptPaused.fill(true);
                }
            }
            if (message.action == actions::Type::NotifyPleasePay &&
                message.numberA >= 0 && message.numberA < rules::MaxPlayers)
                debtMoneyOwed[static_cast<rules::PlayerNumber>(message.numberA)] =
                    message.numberC > 0 ? message.numberC : 0;
            if (message.action == actions::Type::NotifyHousingShortage)
                housingShortageOn = message.numberD != 3;
            if (message.action == actions::Type::NotifyAuctionGoing)
                housingShortageOn = false;
            if (message.action == actions::Type::NotifyPlaceBuilding)
                buildingPlacementOn = true;
            else if (message.action == actions::Type::NotifyActionCompleted &&
                     message.numberA == static_cast<std::int64_t>(actions::Type::BuyHouse))
                buildingPlacementOn = false;
            if (message.action == actions::Type::NotifyPleasePay ||
                message.action == actions::Type::NotifyFreeUnmortgaging ||
                message.action == actions::Type::NotifyPlaceBuilding ||
                message.action == actions::Type::NotifyHousingShortage ||
                message.action == actions::Type::NotifyTradeStarted ||
                message.action == actions::Type::NotifyNewHighBid)
                turnPromptPaused.fill(true);
            if (message.action == actions::Type::NotifyNewHighBid)
                auctionOn = true;
            else if (message.action == actions::Type::NotifyAuctionGoing &&
                     message.numberD >= 3)
                auctionOn = false;

            if (message.action == actions::Type::NotifyPleasePay)
                gracePeriodForHumanActivity = {};
            else if (message.action == actions::Type::NotifyTradeFinished ||
                     (message.action == actions::Type::NotifyPlayerBuySellMort &&
                      message.numberA == rules::NobodyPlayer))
                gracePeriodForHumanActivity = Clock::now();
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
            context.moneyOwed = debtMoneyOwed;
            context.purchasingPlayer = purchasingPlayer;
            context.purchasingProperty = purchasingProperty;
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

        void processPurchaseContextMessage(
            const actions::Message& message) noexcept
        {
            if (message.action == actions::Type::NotifyBuyOrAuctionDecision &&
                message.numberA >= 0 && message.numberA < rules::MaxPlayers &&
                message.numberB >= 0 &&
                message.numberB < static_cast<std::int64_t>(rules::board::SquareType::InJail))
            {
                purchasingPlayer = static_cast<rules::PlayerNumber>(message.numberA);
                purchasingProperty = static_cast<rules::board::SquareType>(message.numberB);
                return;
            }

            if (message.action == actions::Type::NotifySquareOwnership &&
                purchasingPlayer != rules::NobodyPlayer &&
                message.numberA == static_cast<std::int64_t>(purchasingProperty))
            {
                purchasingPlayer = rules::NobodyPlayer;
                purchasingProperty = rules::board::SquareType::Count;
            }
        }

        void processPassiveRuntimeMessage(
            const rules::GameState& state,
            const actions::Message& message) noexcept
        {
            if (message.action == actions::Type::NotifyActionCompleted &&
                message.numberC >= 0 && message.numberC < rules::MaxPlayers)
            {
                const auto player =
                    static_cast<rules::PlayerNumber>(message.numberC);
                const auto completed =
                    static_cast<actions::Type>(message.numberA);
                if (completed == actions::Type::AcceptConfiguration)
                    configurationAcceptInFlight[player] = false;
                else if (completed == actions::Type::CardSeen)
                    cardSeenInFlight[player] = false;
                return;
            }

            if (message.action == actions::Type::NotifyAINeedParametersForSave)
            {
                if (message.numberA <= 0)
                    return;
                const auto requested = static_cast<std::uint32_t>(message.numberA);
                for (rules::PlayerNumber player = 0;
                     player < state.numberOfPlayers && player < rules::MaxPlayers;
                     ++player)
                {
                    if ((requested & (1u << player)) == 0 ||
                        !ui::localplayers::slotIsLocalAIPlayer(player) ||
                        !profileRuntime.playerLoaded[player])
                        continue;

                    save::State persisted{};
                    persisted.profile = profileRuntime.players[player];
                    persisted.timeLastTrade = tradeIngress.turnState[player].timeLastTrade;
                    actions::Message response{};
                    response.action = actions::Type::AISaveParameters;
                    response.fromPlayer = player;
                    response.toPlayer = rules::BankPlayer;
                    if (save::encode(persisted, response.binaryDataA))
                        (void)messaging::sendAction(response);
                }
                return;
            }

            if (message.action == actions::Type::NotifyAIParameters &&
                message.numberA >= 0 && message.numberA < rules::MaxPlayers)
            {
                const auto player = static_cast<rules::PlayerNumber>(message.numberA);
                if (player >= state.numberOfPlayers ||
                    !ui::localplayers::slotIsLocalAIPlayer(player) ||
                    !profileRuntime.playerLoaded[player])
                    return;

                save::State persisted{};
                if (!save::decode(
                        std::span<const std::uint8_t>(message.binaryDataA),
                        persisted))
                    return;

                profileRuntime.players[player] = std::move(persisted.profile);
                tradeIngress.turnState[player].timeLastTrade = persisted.timeLastTrade;
                return;
            }

            if (message.action == actions::Type::NotifyProposedConfiguration)
            {
                if (message.numberB <= 0)
                    return;
                const auto requested =
                    static_cast<std::uint32_t>(message.numberB);
                auto acceptedOptions = state.options;
                if (message.numberC < 1)
                {
                    acceptedOptions.futureRentTradingAllowed = false;
                    acceptedOptions.immunitiesTradingAllowed = false;
                }

                for (rules::PlayerNumber player = 0;
                     player < state.numberOfPlayers && player < rules::MaxPlayers;
                     ++player)
                {
                    const auto bit = 1u << player;
                    if ((requested & bit) == 0 ||
                        !ui::localplayers::slotIsLocalAIPlayer(player) ||
                        configurationAcceptInFlight[player])
                        continue;

                    actions::Message acceptance{};
                    if (rules::configuration::acceptedConfigurationMessage(
                            acceptedOptions, player, false, acceptance) &&
                        messaging::sendAction(acceptance))
                    {
                        configurationAcceptInFlight[player] = true;
                    }
                }
                return;
            }

            if (message.action == actions::Type::NotifyPickedUpCard &&
                message.numberA >= 0 &&
                message.numberA < rules::MaxPlayers)
            {
                const auto player =
                    static_cast<rules::PlayerNumber>(message.numberA);
                if (player < state.numberOfPlayers &&
                    ui::localplayers::slotIsLocalAIPlayer(player) &&
                    !cardSeenInFlight[player] &&
                    messaging::sendAction(
                        actions::Type::CardSeen,
                        player, rules::BankPlayer))
                {
                    cardSeenInFlight[player] = true;
                }
                return;
            }

            if (message.action == actions::Type::NotifyClientResyncInfo)
            {
                resetGlobalTransientStateForResync();
                return;
            }

            if (message.action == actions::Type::NotifyPlayerDeleted &&
                message.numberA >= 0 && message.numberA < rules::MaxPlayers)
            {
                clearPlayerTransientState(
                    static_cast<rules::PlayerNumber>(message.numberA));
                return;
            }

            if (message.action == actions::Type::NotifyJumpToSquare &&
                message.numberA == static_cast<std::int64_t>(
                    rules::board::SquareType::OffBoard) &&
                message.numberC >= 0 && message.numberC < rules::MaxPlayers)
            {
                const auto player =
                    static_cast<rules::PlayerNumber>(message.numberC);
                if (ui::localplayers::slotIsLocalAIPlayer(player))
                    clearPlayerTransientState(player);
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

        [[nodiscard]] std::array<std::uint32_t, 8> retailRandomKeys() noexcept
        {
            std::array<std::uint32_t, 8> keys{};
            for (auto& key : keys)
                key = static_cast<std::uint32_t>(std::rand());
            return keys;
        }

        [[nodiscard]] rules::PlayerNumber buySellMortgagePlayer(
            const rules::GameState&) noexcept
        {
            return economicRuntime.controlOwner;
        }

        [[nodiscard]] bool economicBusy(rules::PlayerNumber player) noexcept
        {
            return player < rules::MaxPlayers &&
                (economicRuntime.hasPendingControlAction[player] ||
                 economicRuntime.controlRequestInFlight[player] ||
                 economicRuntime.actionInFlight[player] ||
                 economicRuntime.controlRestartInFlight[player] ||
                 economicRuntime.controlReleaseInFlight[player]);
        }

        [[nodiscard]] actions::Type economicActionType(
            decision::EconomicActionKind kind) noexcept
        {
            switch (kind)
            {
            case decision::EconomicActionKind::MortgageProperty:
            case decision::EconomicActionKind::UnmortgageProperty:
                return actions::Type::Mortgaging;
            case decision::EconomicActionKind::BuyHouse:
                return actions::Type::BuyHouse;
            case decision::EconomicActionKind::SellBuilding:
                return actions::Type::SellBuildings;
            case decision::EconomicActionKind::None:
                return actions::Type::Tick;
            }
            return actions::Type::Tick;
        }

        [[nodiscard]] bool sendEconomicAction(
            rules::PlayerNumber player,
            const decision::EconomicActionPlan& plan) noexcept
        {
            if (!plan.acted() || player >= rules::MaxPlayers)
                return false;
            const auto action = economicActionType(plan.kind);
            if (action == actions::Type::Tick ||
                !messaging::sendAction(
                    action, player, rules::BankPlayer,
                    static_cast<std::int64_t>(plan.square)))
                return false;
            economicRuntime.actionInFlight[player] = true;
            if (plan.kind == decision::EconomicActionKind::BuyHouse ||
                plan.kind == decision::EconomicActionKind::SellBuilding)
                lastBuildingAction[player] = plan.square;
            return true;
        }

        [[nodiscard]] bool queueEconomicAction(
            const rules::GameState& state,
            rules::PlayerNumber player,
            const decision::EconomicActionPlan& plan) noexcept
        {
            if (!plan.acted() || player >= state.numberOfPlayers || economicBusy(player))
                return false;
            const auto control = buySellMortgagePlayer(state);
            if (control == player)
                return sendEconomicAction(player, plan);
            if (control != rules::NobodyPlayer)
                return false;

            economicRuntime.pendingControlAction[player] = plan;
            economicRuntime.hasPendingControlAction[player] = true;
            if (!messaging::sendAction(
                    actions::Type::PlayerBuySellMort,
                    player, rules::BankPlayer))
            {
                economicRuntime.pendingControlAction[player] = {};
                economicRuntime.hasPendingControlAction[player] = false;
                return false;
            }
            economicRuntime.controlRequestInFlight[player] = true;
            return true;
        }

        [[nodiscard]] bool playerPayingDebt(
            const rules::GameState&, rules::PlayerNumber player) noexcept
        {
            return player < rules::MaxPlayers && debtMoneyOwed[player] > 0;
        }

        struct EconomicDecision
        {
            decision::EconomicActionPlan plan{};
            bool defer{};
        };

        [[nodiscard]] EconomicDecision nextEconomicDecision(
            const rules::GameState& state,
            rules::PlayerNumber player,
            const profile::ConfigContext& context) noexcept
        {
            EconomicDecision result{};
            if (player >= state.numberOfPlayers || !profileRuntime.playerLoaded[player])
                return result;

            const auto& strategy = profileRuntime.players[player];
            const auto buy = decision::shouldBuyHouse(
                state, player, strategy.cashStrategy, strategy.minCashOnHand,
                strategy.housingPurchaseStrategy, context.moneyOwed[player]);
            if (buy == decision::HousePurchaseDecision::Later)
            {
                result.defer = true;
                return result;
            }
            if (buy == decision::HousePurchaseDecision::Yes)
            {
                result.plan = decision::planBuyHouseAction(
                    state, player, strategy.cashStrategy, strategy.minCashOnHand,
                    strategy.housingPurchaseStrategy, context.moneyOwed[player]);
                if (result.plan.acted())
                    return result;
            }

            if (decision::shouldUnmortgageProperty(
                    state, player, strategy.cashStrategy, strategy.minCashOnHand,
                    context.moneyOwed[player]))
            {
                result.plan = decision::planUnmortgagePropertyAction(
                    state, player, strategy.cashStrategy, strategy.minCashOnHand,
                    context.moneyOwed[player]);
            }
            return result;
        }

        [[nodiscard]] bool sendEconomicControlDone(
            rules::PlayerNumber player) noexcept
        {
            if (player >= rules::MaxPlayers ||
                economicRuntime.controlReleaseInFlight[player])
                return false;
            if (!messaging::sendAction(
                    actions::Type::PlayerDoneBuySellMort,
                    player, rules::BankPlayer))
                return false;
            economicRuntime.controlReleaseInFlight[player] = true;
            return true;
        }

        void runControlledEconomicStep(
            const rules::GameState& state,
            rules::PlayerNumber player) noexcept
        {
            if (player >= state.numberOfPlayers ||
                economicRuntime.actionInFlight[player] ||
                economicRuntime.controlReleaseInFlight[player])
                return;

            if (auctionOn || state.tradeInProgress || playerPayingDebt(state, player) ||
                tradeIngress.counterRuntime.sending.state !=
                    trade::SendingTradeState::Nothing)
            {
                (void)sendEconomicControlDone(player);
                return;
            }

            auto context = makeConfigContext();
            const auto next = nextEconomicDecision(state, player, context);
            if (next.defer || !next.plan.acted())
            {
                (void)sendEconomicControlDone(player);
                return;
            }
            (void)sendEconomicAction(player, next.plan);
        }
        void processEconomicRuntimeMessage(
            const rules::GameState& state,
            const actions::Message& message) noexcept
        {
            if (message.action == actions::Type::NotifyActionCompleted &&
                message.numberC >= 0 && message.numberC < rules::MaxPlayers)
            {
                const auto player = static_cast<rules::PlayerNumber>(message.numberC);
                const auto completed = static_cast<actions::Type>(message.numberA);
                if (completed == actions::Type::PlayerBuySellMort)
                {
                    economicRuntime.controlRequestInFlight[player] = false;
                    if (message.numberB == 0)
                    {
                        economicRuntime.pendingControlAction[player] = {};
                        economicRuntime.hasPendingControlAction[player] = false;
                    }
                }
                else if (completed == actions::Type::BuyHouse ||
                         completed == actions::Type::Mortgaging ||
                         completed == actions::Type::SellBuildings)
                {
                    economicRuntime.actionInFlight[player] = false;
                    // Ai.cpp AI_Action_Completed resends the controlling player's
                    // phase after economic actions, including failed actions.
                    // The free-unmortgage owner below has its own restart path.
                    const auto owner = buySellMortgagePlayer(state);
                    if (ui::localplayers::slotIsLocalAIPlayer(player) &&
                        owner < state.numberOfPlayers &&
                        !freeUnmortgageInFlight[player] &&
                        (completed != actions::Type::BuyHouse || !auctionOn) &&
                        !economicRuntime.controlRestartInFlight[owner])
                    {
                        economicRuntime.controlRestartInFlight[owner] =
                            messaging::sendAction(
                                actions::Type::RestartPhase, owner, rules::BankPlayer);
                    }
                }
                else if (completed == actions::Type::PlayerDoneBuySellMort)
                {
                    economicRuntime.controlReleaseInFlight[player] = false;
                }
                return;
            }
            if (message.action != actions::Type::NotifyPlayerBuySellMort)
                return;

            if (message.numberA == rules::NobodyPlayer)
            {
                for (rules::PlayerNumber player = 0; player < rules::MaxPlayers; ++player)
                {
                    economicRuntime.controlRestartInFlight[player] = false;
                    economicRuntime.controlReleaseInFlight[player] = false;
                }
                return;
            }
            if (message.numberA < 0 || message.numberA >= state.numberOfPlayers)
                return;

            const auto player = static_cast<rules::PlayerNumber>(message.numberA);
            economicRuntime.controlRestartInFlight[player] = false;
            if (buySellMortgagePlayer(state) != player ||
                !ui::localplayers::slotIsLocalAIPlayer(player) ||
                !profileRuntime.playerLoaded[player])
                return;

            economicRuntime.controlRequestInFlight[player] = false;
            if (economicRuntime.actionInFlight[player] ||
                economicRuntime.controlReleaseInFlight[player])
                return;

            if (economicRuntime.hasPendingControlAction[player])
            {
                const auto pending = economicRuntime.pendingControlAction[player];
                if (sendEconomicAction(player, pending))
                {
                    economicRuntime.pendingControlAction[player] = {};
                    economicRuntime.hasPendingControlAction[player] = false;
                }
                return;
            }

            runControlledEconomicStep(state, player);
        }

        void processFreeUnmortgageMessage(
            const rules::GameState& state,
            const actions::Message& message) noexcept
        {
            if (message.action == actions::Type::NotifyActionCompleted &&
                message.numberC >= 0 && message.numberC < rules::MaxPlayers)
            {
                const auto player = static_cast<rules::PlayerNumber>(message.numberC);
                const auto completed = static_cast<actions::Type>(message.numberA);
                if (completed == actions::Type::Mortgaging &&
                    freeUnmortgageInFlight[player])
                {
                    freeUnmortgageInFlight[player] = false;
                    (void)messaging::sendAction(
                        actions::Type::RestartPhase, player, rules::BankPlayer);
                    return;
                }
                if (completed == actions::Type::FreeUnmortgageDone)
                {
                    freeUnmortgageDoneInFlight[player] = false;
                    return;
                }
            }

            if (message.action != actions::Type::NotifyFreeUnmortgaging ||
                message.numberA < 0 || message.numberA >= state.numberOfPlayers ||
                message.numberB == 0)
                return;

            const auto player = static_cast<rules::PlayerNumber>(message.numberA);
            if (!ui::localplayers::slotIsLocalAIPlayer(player) ||
                !profileRuntime.playerLoaded[player] ||
                freeUnmortgageInFlight[player] || freeUnmortgageDoneInFlight[player])
                return;

            const auto& strategy = profileRuntime.players[player];
            auto context = makeConfigContext();
            if (decision::shouldUnmortgageProperty(
                    state, player, strategy.cashStrategy, strategy.minCashOnHand,
                    context.moneyOwed[player]))
            {
                const auto plan = decision::planUnmortgagePropertyAction(
                    state, player, strategy.cashStrategy, strategy.minCashOnHand,
                    context.moneyOwed[player]);
                if (plan.acted() && sendEconomicAction(player, plan))
                {
                    freeUnmortgageInFlight[player] = true;
                    return;
                }
            }

            if (messaging::sendAction(
                    actions::Type::FreeUnmortgageDone,
                    player, rules::BankPlayer))
            {
                freeUnmortgageDoneInFlight[player] = true;
            }
        }

        void processBuyDecisionMessage(
            const rules::GameState& state,
            const actions::Message& message) noexcept
        {
            if (message.action == actions::Type::NotifyActionCompleted &&
                message.numberA == static_cast<std::int64_t>(actions::Type::BuyOrAuctionDecision) &&
                message.numberC >= 0 && message.numberC < rules::MaxPlayers)
            {
                buyDecisionInFlight[static_cast<rules::PlayerNumber>(message.numberC)] = false;
                return;
            }
            if (message.action != actions::Type::NotifyBuyOrAuctionDecision ||
                message.numberA < 0 || message.numberA >= state.numberOfPlayers ||
                message.numberB < 0 ||
                message.numberB >= static_cast<std::int64_t>(rules::board::SquareType::InJail))
                return;

            const auto player = static_cast<rules::PlayerNumber>(message.numberA);
            if (!ui::localplayers::slotIsLocalAIPlayer(player) ||
                !profileRuntime.playerLoaded[player] || buyDecisionInFlight[player])
                return;
            const auto property = static_cast<rules::board::SquareType>(message.numberB);
            const auto& strategy = profileRuntime.players[player];
            const auto context = makeConfigContext();
            const bool buy = decision::shouldBuyProperty(
                state, player, property, strategy.cashStrategy,
                strategy.minCashOnHand, context.moneyOwed);
            if (messaging::sendAction(
                    actions::Type::BuyOrAuctionDecision,
                    player, rules::BankPlayer, buy ? 1 : 0))
                buyDecisionInFlight[player] = true;
        }

        void processAuctionMessage(
            const rules::GameState& state,
            const actions::Message& message) noexcept
        {
            if (message.action == actions::Type::NotifyActionCompleted &&
                message.numberA == static_cast<std::int64_t>(actions::Type::Bid))
            {
                if (message.numberC == auctionBiddingPlayer)
                {
                    auctionBidInFlight = false;
                    auctionBiddingPlayer = rules::NobodyPlayer;
                }
                return;
            }
            if ((message.action != actions::Type::NotifyNewHighBid &&
                 message.action != actions::Type::NotifyAuctionGoing) ||
                auctionBidInFlight || state.numberOfPlayers == 0 ||
                state.numberOfPlayers > rules::MaxPlayers)
                return;
            if (message.action == actions::Type::NotifyAuctionGoing && message.numberD >= 3)
                return;
            if (message.numberB < 0 || message.numberC < 0 ||
                message.numberC >= static_cast<std::int64_t>(rules::board::SquareType::Count))
                return;

            const auto currentBidder = message.numberA >= 0 &&
                message.numberA < state.numberOfPlayers
                ? static_cast<rules::PlayerNumber>(message.numberA)
                : rules::NobodyPlayer;
            const auto item = static_cast<rules::board::SquareType>(message.numberC);
            const auto start = static_cast<rules::PlayerNumber>(
                (static_cast<unsigned long long>(std::rand()) * state.numberOfPlayers) /
                (static_cast<unsigned long long>(RAND_MAX) + 1ull));
            const auto context = makeConfigContext();
            for (std::size_t checked = 0; checked < state.numberOfPlayers; ++checked)
            {
                const auto player = static_cast<rules::PlayerNumber>(
                    (start + checked) % state.numberOfPlayers);
                if (!ui::localplayers::slotIsLocalAIPlayer(player) ||
                    !profileRuntime.playerLoaded[player] ||
                    state.players[player].currentSquare == static_cast<std::uint8_t>(
                        rules::board::SquareType::OffBoard))
                    continue;

                const auto& strategy = profileRuntime.players[player];
                decision::AuctionBidConfig config{};
                config.evaluation = profile::makeTradeEvaluationConfig(
                    profileRuntime.players, player, context);
                config.housingPurchaseStrategy = strategy.housingPurchaseStrategy;
                config.monopolySuicideFactor = strategy.monopolySuicideFactor;
                const auto bid = decision::bidForAuctionItem(
                    state, player, item, message.numberB, currentBidder, config,
                    (std::rand() % 2) != 0);
                if (bid == 0)
                    continue;
                if (messaging::sendAction(
                        actions::Type::Bid, player, rules::BankPlayer, bid))
                {
                    auctionBidInFlight = true;
                    auctionBiddingPlayer = player;
                }
                return;
            }
        }

        void processHousingShortageMessage(
            const rules::GameState& state,
            const actions::Message& message) noexcept
        {
            if (message.action == actions::Type::NotifyActionCompleted &&
                message.numberC >= 0 && message.numberC < rules::MaxPlayers)
            {
                const auto player = static_cast<rules::PlayerNumber>(message.numberC);
                const auto completed = static_cast<actions::Type>(message.numberA);
                if (completed == actions::Type::StartHousingAuction)
                    housingAuctionRequestInFlight[player] = false;
                else if (completed == actions::Type::BuyHouse && buildingPlacementInFlight[player])
                    buildingPlacementInFlight[player] = false;
                return;
            }
            if (message.action == actions::Type::NotifyPlaceBuilding)
            {
                if (message.numberA < 0 || message.numberA >= state.numberOfPlayers ||
                    message.numberC <= 0)
                    return;
                const auto player = static_cast<rules::PlayerNumber>(message.numberA);
                if (!ui::localplayers::slotIsLocalAIPlayer(player) ||
                    !profileRuntime.playerLoaded[player] || buildingPlacementInFlight[player])
                    return;
                const auto& strategy = profileRuntime.players[player];
                const auto context = makeConfigContext();
                const auto building = message.numberB > 0
                    ? decision::HousingAuctionBuilding::Hotel
                    : decision::HousingAuctionBuilding::House;
                const auto square = decision::chooseHousingAuctionSquare(
                    state, player, building, strategy.cashStrategy,
                    strategy.minCashOnHand,
                    static_cast<rules::board::PropertySet>(message.numberC), true,
                    context.moneyOwed[player]);
                if (square && messaging::sendAction(
                        actions::Type::BuyHouse, player, rules::BankPlayer,
                        static_cast<std::int64_t>(*square)))
                    buildingPlacementInFlight[player] = true;
                return;
            }
            if (message.action != actions::Type::NotifyHousingShortage ||
                message.numberD >= 3 || message.numberE <= 0)
                return;

            const auto allowed = static_cast<std::uint32_t>(message.numberE);
            const auto originalBuyer = message.numberA >= 0 &&
                message.numberA < state.numberOfPlayers
                ? static_cast<rules::PlayerNumber>(message.numberA)
                : rules::NobodyPlayer;
            const auto building = message.numberC > 0
                ? decision::HousingAuctionBuilding::Hotel
                : decision::HousingAuctionBuilding::House;
            const auto context = makeConfigContext();
            for (rules::PlayerNumber player = 0; player < state.numberOfPlayers; ++player)
            {
                if ((allowed & (1u << player)) == 0 || player == originalBuyer ||
                    !ui::localplayers::slotIsLocalAIPlayer(player) ||
                    !profileRuntime.playerLoaded[player] ||
                    housingAuctionRequestInFlight[player] || economicBusy(player))
                    continue;
                const auto& strategy = profileRuntime.players[player];
                if (decision::shouldBuyHouse(
                        state, player, strategy.cashStrategy, strategy.minCashOnHand,
                        strategy.housingPurchaseStrategy, context.moneyOwed[player]) ==
                    decision::HousePurchaseDecision::No)
                    continue;
                const auto square = decision::chooseHousingAuctionSquare(
                    state, player, building, strategy.cashStrategy,
                    strategy.minCashOnHand, 0, false, context.moneyOwed[player]);
                if (!square)
                    continue;
                if (messaging::sendAction(
                        actions::Type::StartHousingAuction,
                        player, rules::BankPlayer))
                    housingAuctionRequestInFlight[player] = true;
                return;
            }
        }

        void processTaxDecisionMessage(
            const rules::GameState& state,
            const actions::Message& message) noexcept
        {
            if (message.action == actions::Type::NotifyActionCompleted &&
                message.numberA == static_cast<std::int64_t>(actions::Type::TaxDecision) &&
                message.numberC >= 0 && message.numberC < rules::MaxPlayers)
            {
                taxDecisionInFlight[static_cast<rules::PlayerNumber>(message.numberC)] = false;
                return;
            }
            if (message.action != actions::Type::NotifyFlatOrFractionTaxDecision ||
                message.numberA < 0 || message.numberA >= state.numberOfPlayers)
                return;

            const auto player = static_cast<rules::PlayerNumber>(message.numberA);
            if (!ui::localplayers::slotIsLocalAIPlayer(player) ||
                !profileRuntime.playerLoaded[player] || taxDecisionInFlight[player])
                return;

            const auto percentage = decision::chooseFractionTax(state, player);
            if (!percentage)
                return;
            if (messaging::sendAction(
                    actions::Type::TaxDecision, player, rules::BankPlayer,
                    *percentage ? 1 : 0))
            {
                taxDecisionInFlight[player] = true;
            }
        }

        void processJailDecisionMessage(
            const rules::GameState& state,
            const actions::Message& message) noexcept
        {
            if (message.action == actions::Type::NotifyActionCompleted &&
                message.numberA == static_cast<std::int64_t>(actions::Type::ExitJailDecision) &&
                message.numberC >= 0 && message.numberC < rules::MaxPlayers)
            {
                jailDecisionInFlight[static_cast<rules::PlayerNumber>(message.numberC)] = false;
                return;
            }
            if (message.action != actions::Type::NotifyJailExitChoice ||
                message.numberA < 0 || message.numberA >= state.numberOfPlayers)
                return;

            const auto player = static_cast<rules::PlayerNumber>(message.numberA);
            if (!ui::localplayers::slotIsLocalAIPlayer(player) ||
                !profileRuntime.playerLoaded[player] || jailDecisionInFlight[player])
                return;

            const auto choice = decision::chooseJailExitChoice(
                state, player,
                message.numberB != 0,
                message.numberC != 0,
                message.numberD != 0);
            if (!choice)
                return;
            if (messaging::sendAction(
                    actions::Type::ExitJailDecision,
                    player, rules::BankPlayer,
                    static_cast<std::int64_t>(*choice)))
            {
                jailDecisionInFlight[player] = true;
            }
        }

        [[nodiscard]] bool autonomousTradeBlocked(
            const rules::GameState& state, rules::PlayerNumber player) noexcept
        {
            if (auctionOn || state.tradeInProgress || economicBusy(player) ||
                playerPayingDebt(state, player) ||
                tradeIngress.counterRuntime.sending.state !=
                    trade::SendingTradeState::Nothing ||
                state.players[player].currentSquare ==
                    static_cast<std::uint8_t>(rules::board::SquareType::OffBoard))
                return true;

            if (housingShortageOn || buildingPlacementOn ||
                freeUnmortgageInFlight[player] || freeUnmortgageDoneInFlight[player])
                return true;

            const auto controlPlayer = buySellMortgagePlayer(state);
            if (controlPlayer != rules::NobodyPlayer && controlPlayer != player)
                return true;

            return state.options.aiTakesTimeToThink &&
                gracePeriodForHumanActivity != Clock::time_point{} &&
                Clock::now() - gracePeriodForHumanActivity < std::chrono::seconds(10);
        }

        void maybeSpendAutonomousAssets(
            const rules::GameState& state,
            const actions::Message& message) noexcept
        {
            if (message.action != actions::Type::Tick &&
                !(message.action == actions::Type::NotifyStartTurn &&
                  !state.options.aiTakesTimeToThink))
                return;

            auto context = makeConfigContext();
            for (rules::PlayerNumber player = 0; player < state.numberOfPlayers; ++player)
            {
                if (!ui::localplayers::slotIsLocalAIPlayer(player) ||
                    !profileRuntime.playerLoaded[player] || economicBusy(player) ||
                    pendingTurnPrompt[player] != PendingTurnPrompt::None ||
                    turnActionInFlight[player] || autonomousTradeBlocked(state, player))
                    continue;

                const auto next = nextEconomicDecision(state, player, context);
                if (next.defer || !next.plan.acted())
                    continue;
                if (queueEconomicAction(state, player, next.plan))
                    return;
            }
        }

        [[nodiscard]] decision::SemiImportantTradeInputs makeSemiImportantAttemptInputs(
            const rules::GameState& state, rules::PlayerNumber player,
            std::uint8_t importance, const profile::Profile& strategy,
            const profile::ConfigContext& context) noexcept
        {
            decision::SemiImportantTradeInputs inputs{};
            const auto assets = ai::liquidAssets(
                state, player, false, false, context.moneyOwed[player]);
            if ((importance & trade::TradeForCash) != 0 ||
                state.players[player].aiPlayerLevel != 3)
            {
                const auto keys = retailRandomKeys();
                inputs.wantedGroups = trade::orderMonopolyImportance(
                    assets, trade::MonopolySortOrder::Random, keys);
            }
            else
            {
                inputs.wantedGroups = trade::orderMonopolyImportance(
                    assets, trade::MonopolySortOrder::Descending);
            }
            inputs.deriveOfferedGroups = true;
            inputs.wantedPropertyRoll = static_cast<std::uint32_t>(std::rand());
            inputs.offeredPropertyRoll = static_cast<std::uint32_t>(std::rand());
            inputs.importance = importance;
            inputs.minimumNonmonopolyTradeAttitude =
                strategy.minimumNonmonopolyTradeAttitude;
            return inputs;
        }

        struct SemiImportantPartnerOrder
        {
            std::array<rules::PlayerNumber, rules::MaxPlayers> players{};
            std::size_t count{};
        };

        [[nodiscard]] SemiImportantPartnerOrder semiImportantPartnerOrder(
            const rules::GameState& state, rules::PlayerNumber player,
            std::span<const double> attitudes,
            rules::PlayerNumber excludedPlayer) noexcept
        {
            SemiImportantPartnerOrder result{};
            std::array<rules::PlayerNumber, rules::MaxPlayers> candidates{};
            std::size_t candidateCount{};
            double totalWeight{};
            for (rules::PlayerNumber candidate = 0;
                 candidate < state.numberOfPlayers; ++candidate)
            {
                if (candidate == player || candidate == excludedPlayer ||
                    state.players[candidate].currentSquare ==
                        static_cast<std::uint8_t>(rules::board::SquareType::OffBoard) ||
                    candidate >= attitudes.size() || attitudes[candidate] < -1.0)
                    continue;
                candidates[candidateCount++] = candidate;
                totalWeight += (attitudes[candidate] + 1.0) / 2.0;
            }
            if (candidateCount == 0 || totalWeight <= 0.0)
                return result;

            double choice = retailCounterRoll();
            std::size_t start{};
            for (; start < candidateCount; ++start)
            {
                choice -= (attitudes[candidates[start]] + 1.0) / 2.0 / totalWeight;
                if (choice <= 0.0)
                    break;
            }
            if (start >= candidateCount)
                start = candidateCount - 1;
            for (std::size_t offset = 0; offset < candidateCount; ++offset)
                result.players[result.count++] =
                    candidates[(start + offset) % candidateCount];
            return result;
        }

        [[nodiscard]] bool buildSemiImportantTradeRetail(
            const rules::GameState& state, rules::PlayerNumber player,
            std::uint8_t importance, rules::PlayerNumber excludedPlayer,
            const profile::Profile& strategy,
            const profile::ConfigContext& context,
            const trade::PropertySets& properties,
            const decision::MonopolyProposalConfig& config,
            trade::TradeProposalList& proposal) noexcept
        {
            const auto order = semiImportantPartnerOrder(
                state, player, strategy.playerAttitude, excludedPlayer);
            for (std::size_t index = 0; index < order.count; ++index)
            {
                auto attempt = makeSemiImportantAttemptInputs(
                    state, player, importance, strategy, context);
                attempt.excludedPlayer = excludedPlayer;
                attempt.requiredTarget = order.players[index];
                attempt.partnerRoll = 0.0;

                trade::TradeProposalList next{};
                if (decision::buildSemiImportantTrade(
                        state, player, strategy.playerAttitude, properties,
                        attempt, next, config) &&
                    trade::tradeIsProper(state, next))
                {
                    proposal = next;
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool tryProposeAutonomousTradeForPlayer(
            const rules::GameState& state, rules::PlayerNumber player,
            const profile::ConfigContext& context) noexcept
        {
            if (player >= state.numberOfPlayers || !context.localAIPlayer[player] ||
                autonomousTradeBlocked(state, player))
                return false;

            const auto& strategy = profileRuntime.players[player];
            const bool shouldGiveAway = decision::shouldGiveAwayMonopoly(
                state, player, strategy.cashStrategy, strategy.minCashOnHand,
                strategy.maxHousesPerSquareForGiveAway, context.moneyOwed);

            trade::TradeCadenceInputs cadence{};
            cadence.playerSendingTrade = tradeIngress.counterRuntime.sending.state !=
                trade::SendingTradeState::Nothing;
            cadence.buySellMortgagePlayer = buySellMortgagePlayer(state);
            cadence.shouldGiveAwayMonopoly = shouldGiveAway;
            cadence.giveAwayProbability = strategy.proposeMonopolyGiveAwayProbability;
            cadence.monopolyProbability = strategy.monopolyTradeProbability;
            cadence.proposalProbability = strategy.proposeTradeProbability;

            bool wantsTrade{};
            if (shouldGiveAway)
            {
                cadence.giveAwayRoll = retailCounterRoll();
                cadence.proposalRoll = 2.0;
                wantsTrade = trade::shouldTrade(
                    state, player, 0, tradeIngress.turnState[player].timeLastTrade,
                    strategy.maxTrades, cadence);
                cadence.shouldGiveAwayMonopoly = false;
            }
            if (!wantsTrade)
            {
                cadence.proposalRoll = retailCounterRoll();
                wantsTrade = trade::shouldTrade(
                    state, player, 0, tradeIngress.turnState[player].timeLastTrade,
                    strategy.maxTrades, cadence);
            }
            if (!wantsTrade)
                return false;

            trade::PropertySets properties{};
            for (rules::PlayerNumber owner = 0; owner < state.numberOfPlayers; ++owner)
                properties[owner] = ai::propertiesOwnedByPlayer(state, owner);

            decision::ProactiveTradeInputs inputs{};
            inputs.shouldGiveAwayMonopoly = shouldGiveAway;
            const auto assets = ai::liquidAssets(
                state, player, false, false, context.moneyOwed[player]);
            if (state.players[player].aiPlayerLevel == 3)
                inputs.monopolyGroups = trade::orderMonopolyImportance(
                    assets, trade::MonopolySortOrder::Descending);
            else
            {
                const auto keys = retailRandomKeys();
                inputs.monopolyGroups = trade::orderMonopolyImportance(
                    assets, trade::MonopolySortOrder::Random, keys);
            }

            const auto config = profile::makeMonopolyProposalConfig(
                profileRuntime.players, player, context);
            auto proposal = decision::buildProactiveTrade(
                state, player, strategy.playerAttitude, properties, inputs, config);
            if (proposal.kind == decision::ProactiveTradeKind::NeedsSemiImportant)
            {
                trade::TradeProposalList semiProposal{};
                if (!buildSemiImportantTradeRetail(
                        state, player, 0, rules::NobodyPlayer, strategy, context,
                        properties, config, semiProposal))
                    return false;
                return trade::sendProactiveTrade(
                    state, player, semiProposal, strategy, tradeIngress);
            }
            if (proposal.kind == decision::ProactiveTradeKind::None)
                return false;
            return trade::sendProactiveTrade(
                state, player, proposal.proposal, strategy, tradeIngress);
        }

        [[nodiscard]] bool sendDebtEconomicAction(
            rules::PlayerNumber player,
            const decision::EconomicActionPlan& plan) noexcept
        {
            if (!plan.acted() || player >= rules::MaxPlayers ||
                debtActionInFlight[player])
                return false;

            actions::Type action{};
            if (plan.kind == decision::EconomicActionKind::MortgageProperty)
                action = actions::Type::Mortgaging;
            else if (plan.kind == decision::EconomicActionKind::SellBuilding)
                action = actions::Type::SellBuildings;
            else
                return false;

            if (!messaging::sendAction(
                    action, player, rules::BankPlayer,
                    static_cast<std::int64_t>(plan.square), 0, 0, 1))
                return false;
            debtActionInFlight[player] = true;
            if (plan.kind == decision::EconomicActionKind::SellBuilding)
                lastBuildingAction[player] = plan.square;
            return true;
        }

        [[nodiscard]] bool tryProposeDebtTrade(
            const rules::GameState& state,
            rules::PlayerNumber player,
            rules::PlayerNumber creditor,
            std::uint8_t importance,
            const profile::ConfigContext& context) noexcept
        {
            if (player >= state.numberOfPlayers ||
                !profileRuntime.playerLoaded[player] ||
                state.tradeInProgress ||
                tradeIngress.counterRuntime.sending.state !=
                    trade::SendingTradeState::Nothing)
                return false;

            const auto& strategy = profileRuntime.players[player];
            if ((importance & trade::TradeDesperate) == 0)
            {
                trade::TradeCadenceInputs cadence{};
                cadence.buySellMortgagePlayer = buySellMortgagePlayer(state);
                if (!trade::shouldTrade(
                        state, player, importance,
                        tradeIngress.turnState[player].timeLastTrade,
                        strategy.maxTrades, cadence))
                    return false;
            }

            trade::PropertySets properties{};
            for (rules::PlayerNumber owner = 0;
                 owner < state.numberOfPlayers; ++owner)
                properties[owner] = ai::propertiesOwnedByPlayer(state, owner);

            decision::ProactiveTradeInputs inputs{};
            inputs.importance = importance;
            inputs.excludedPlayer = creditor;
            const auto assets = ai::liquidAssets(
                state, player, false, false, context.moneyOwed[player]);
            if (state.players[player].aiPlayerLevel == 3)
                inputs.monopolyGroups = trade::orderMonopolyImportance(
                    assets, trade::MonopolySortOrder::Descending);
            else
            {
                const auto keys = retailRandomKeys();
                inputs.monopolyGroups = trade::orderMonopolyImportance(
                    assets, trade::MonopolySortOrder::Random, keys);
            }

            const auto config = profile::makeMonopolyProposalConfig(
                profileRuntime.players, player, context);
            auto proposal = decision::buildProactiveTrade(
                state, player, strategy.playerAttitude, properties, inputs, config);
            if (proposal.kind == decision::ProactiveTradeKind::NeedsSemiImportant)
            {
                trade::TradeProposalList semiProposal{};
                if (!buildSemiImportantTradeRetail(
                        state, player, importance, creditor, strategy, context,
                        properties, config, semiProposal))
                    return false;
                return trade::sendProactiveTrade(
                    state, player, semiProposal, strategy, tradeIngress);
            }
            if (proposal.kind == decision::ProactiveTradeKind::None)
                return false;
            return trade::sendProactiveTrade(
                state, player, proposal.proposal, strategy, tradeIngress);
        }

        void processDebtMessage(
            const rules::GameState& state,
            const actions::Message& message) noexcept
        {
            if (message.action == actions::Type::NotifyPlayerDeleted &&
                message.numberA >= 0 && message.numberA < rules::MaxPlayers)
            {
                const auto player = static_cast<rules::PlayerNumber>(message.numberA);
                debtActionInFlight[player] = false;
                debtMoneyOwed[player] = 0;
                return;
            }

            if (message.action == actions::Type::NotifyActionCompleted &&
                message.numberC >= 0 && message.numberC < rules::MaxPlayers)
            {
                const auto player = static_cast<rules::PlayerNumber>(message.numberC);
                const auto completed = static_cast<actions::Type>(message.numberA);
                if (completed == actions::Type::Mortgaging ||
                    completed == actions::Type::SellBuildings)
                    debtActionInFlight[player] = false;

                if (completed == actions::Type::NotifyPleasePay ||
                    completed == actions::Type::GoBankrupt)
                {
                    debtActionInFlight[player] = false;
                    debtMoneyOwed[player] = 0;
                }
                return;
            }

            if (message.action == actions::Type::NotifyDecomposeSale)
            {
                if (message.numberA < 0 || message.numberA >= rules::MaxPlayers)
                    return;
                const auto player = static_cast<rules::PlayerNumber>(message.numberA);
                if (!ui::localplayers::slotIsLocalAIPlayer(player) ||
                    !profileRuntime.playerLoaded[player] ||
                    debtActionInFlight[player] || economicRuntime.actionInFlight[player])
                    return;
                const auto plan = decision::planHouseSaleAction(
                    state, lastBuildingAction[player]);
                if (plan.acted())
                    (void)sendDebtEconomicAction(player, plan);
                return;
            }

            if (message.action != actions::Type::NotifyPleasePay ||
                message.numberA < 0 || message.numberA >= rules::MaxPlayers)
                return;

            const auto player = static_cast<rules::PlayerNumber>(message.numberA);
            if (player >= state.numberOfPlayers ||
                !ui::localplayers::slotIsLocalAIPlayer(player) ||
                !profileRuntime.playerLoaded[player] || debtActionInFlight[player] ||
                state.tradeInProgress ||
                tradeIngress.counterRuntime.sending.state !=
                    trade::SendingTradeState::Nothing)
                return;

            debtMoneyOwed[player] = message.numberC > 0 ? message.numberC : 0;
            auto context = makeConfigContext();

            if (message.numberB < 0 ||
                message.numberB > static_cast<std::int64_t>(rules::BankPlayer))
            {
                debtMoneyOwed[player] = 0;
                return;
            }
            const auto creditor = static_cast<rules::PlayerNumber>(message.numberB);
            for (rules::PlayerNumber current = 0;
                 current < rules::MaxPlayers; ++current)
                context.localAIPlayer[current] = context.localAIPlayer[current] &&
                    profileRuntime.playerLoaded[current];

            const auto first = decision::planDebtLiquidationStep(
                state, player, false);
            if (first.acted())
            {
                (void)sendDebtEconomicAction(player, first);
                return;
            }

            if (tryProposeDebtTrade(
                    state, player, creditor,
                    trade::TradeSomewhatImportant | trade::TradeForCash, context))
                return;

            const auto lastResort = decision::planDebtLiquidationStep(
                state, player, true);
            if (lastResort.acted())
            {
                (void)sendDebtEconomicAction(player, lastResort);
                return;
            }

            if (tryProposeDebtTrade(
                    state, player, creditor,
                    trade::TradeDesperate | trade::TradeForCash, context))
                return;

            if (messaging::sendAction(
                    actions::Type::GoBankrupt,
                    player, rules::BankPlayer))
                debtActionInFlight[player] = true;
        }

        void maybeProposeAutonomousTrade(
            const rules::GameState& state, const actions::Message& message) noexcept
        {
            if (message.action != actions::Type::Tick &&
                !(message.action == actions::Type::NotifyStartTurn &&
                  !state.options.aiTakesTimeToThink))
                return;
            auto context = makeConfigContext();
            for (rules::PlayerNumber current = 0; current < rules::MaxPlayers; ++current)
                context.localAIPlayer[current] = context.localAIPlayer[current] &&
                    profileRuntime.playerLoaded[current];
            for (rules::PlayerNumber player = 0; player < state.numberOfPlayers; ++player)
            {
                if (pendingTurnPrompt[player] != PendingTurnPrompt::None ||
                    turnActionInFlight[player])
                    continue;
                if (tryProposeAutonomousTradeForPlayer(state, player, context))
                    return;
            }
        }

        [[nodiscard]] bool sendPendingTurnAction(
            rules::PlayerNumber player) noexcept
        {
            if (player >= rules::MaxPlayers || turnActionInFlight[player])
                return false;
            const auto pending = pendingTurnPrompt[player];
            if (pending == PendingTurnPrompt::None)
                return false;
            const auto action = pending == PendingTurnPrompt::RollDice
                ? actions::Type::RollDice : actions::Type::EndTurn;
            if (!messaging::sendAction(action, player, rules::BankPlayer))
                return false;
            pendingTurnPrompt[player] = PendingTurnPrompt::None;
            turnActionInFlight[player] = true;
            return true;
        }

        void attemptPendingTurnPrompt(
            const rules::GameState& state, rules::PlayerNumber player) noexcept
        {
            if (player >= state.numberOfPlayers ||
                pendingTurnPrompt[player] == PendingTurnPrompt::None ||
                turnActionInFlight[player] || !ui::localplayers::slotIsLocalAIPlayer(player) ||
                !profileRuntime.playerLoaded[player])
                return;
            if (economicBusy(player) ||
                tradeIngress.counterRuntime.sending.state != trade::SendingTradeState::Nothing)
                return;
            // A queued prompt belongs to the phase that emitted it. Modal
            // notifications suspend it until RULE emits a fresh turn prompt.
            if (turnPromptPaused[player] ||
                buySellMortgagePlayer(state) != rules::NobodyPlayer ||
                auctionOn || housingShortageOn || buildingPlacementOn ||
                state.tradeInProgress || playerPayingDebt(state, player))
                return;
            auto context = makeConfigContext();
            for (rules::PlayerNumber current = 0; current < rules::MaxPlayers; ++current)
                context.localAIPlayer[current] = context.localAIPlayer[current] &&
                    profileRuntime.playerLoaded[current];

            if (!autonomousTradeBlocked(state, player) &&
                tryProposeAutonomousTradeForPlayer(state, player, context))
                return;

            if (!autonomousTradeBlocked(state, player))
            {
                const auto next = nextEconomicDecision(state, player, context);
                if (!next.defer && next.plan.acted())
                {
                    if (queueEconomicAction(state, player, next.plan))
                        return;
                    return;
                }
            }
            (void)sendPendingTurnAction(player);
        }

        void processTurnPromptMessage(
            const rules::GameState& state, const actions::Message& message) noexcept
        {
            if (message.action == actions::Type::NotifyActionCompleted &&
                message.numberC >= 0 && message.numberC < rules::MaxPlayers)
            {
                const auto completed = static_cast<actions::Type>(message.numberA);
                if (completed == actions::Type::RollDice || completed == actions::Type::EndTurn)
                {
                    const auto player = static_cast<rules::PlayerNumber>(message.numberC);
                    turnActionInFlight[player] = false;
                    pendingTurnPrompt[player] = PendingTurnPrompt::None;
                }
            }

            if (message.action == actions::Type::NotifyPleaseRollDice ||
                message.action == actions::Type::NotifyEndTurn)
            {
                if (message.numberA < 0 || message.numberA >= state.numberOfPlayers)
                    return;
                const auto player = static_cast<rules::PlayerNumber>(message.numberA);
                if (turnActionInFlight[player])
                    return;
                turnPromptPaused[player] = false;
                pendingTurnPrompt[player] = message.action == actions::Type::NotifyPleaseRollDice
                    ? PendingTurnPrompt::RollDice : PendingTurnPrompt::EndTurn;
                attemptPendingTurnPrompt(state, player);
                return;
            }

            if (message.action == actions::Type::NotifyTradeFinished ||
                (message.action == actions::Type::NotifyPlayerBuySellMort &&
                 message.numberA == rules::NobodyPlayer) ||
                message.action == actions::Type::Tick)
            {
                for (rules::PlayerNumber player = 0; player < state.numberOfPlayers; ++player)
                    attemptPendingTurnPrompt(state, player);
            }
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

                // Ai.cpp:3095-3108 waits for every outstanding action of an
                // involved player. Uninvolved AIs may still accept immediately.
                if (trade::playerInvolvedInTrade(tradeIngress.currentTrade[player]) &&
                    pendingActionsForPlayer(player) != 0)
                {
                    if ((tradeIngress.pendingTradeAcceptPlayers & playerBit) == 0)
                        tradeIngress.deferredAcceptancePlayers |= playerBit;
                    return;
                }

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
                    pendingActionsForPlayer(player), auctionOn,
                    preflight, balance, tradeIngress);
                if (!counter.acted())
                    (void)sendTradeAcceptance(player, false, 0);
                return;
            }
        }

        void maybeRestartDeferredAcceptance(
            const actions::Message& message) noexcept
        {
            const auto restart = [](rules::PlayerNumber player)
            {
                const auto bit = 1u << player;
                if ((tradeIngress.deferredAcceptancePlayers & bit) == 0 ||
                    pendingActionsForPlayer(player) != 0)
                    return;
                if (messaging::sendAction(
                        actions::Type::RestartPhase, player, rules::BankPlayer))
                    tradeIngress.deferredAcceptancePlayers &= ~bit;
            };
            // Retry a full outbound queue without losing the deferred request.
            if (message.action == actions::Type::Tick)
            {
                for (rules::PlayerNumber player = 0; player < rules::MaxPlayers; ++player)
                    restart(player);
                return;
            }
            if (message.action != actions::Type::NotifyActionCompleted ||
                message.numberC < 0 || message.numberC >= rules::MaxPlayers)
                return;
            restart(static_cast<rules::PlayerNumber>(message.numberC));
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
                state, player, false, 0.0, pendingActionsForPlayer(player), auctionOn,
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
        economicRuntime = {};
        jailDecisionInFlight.fill(false);
        taxDecisionInFlight.fill(false);
        buyDecisionInFlight.fill(false);
        auctionBidInFlight = false;
        auctionBiddingPlayer = rules::NobodyPlayer;
        housingAuctionRequestInFlight.fill(false);
        buildingPlacementInFlight.fill(false);
        debtActionInFlight.fill(false);
        debtMoneyOwed.fill(0);
        freeUnmortgageInFlight.fill(false);
        freeUnmortgageDoneInFlight.fill(false);
        pendingTurnPrompt.fill(PendingTurnPrompt::None);
        turnActionInFlight.fill(false);
        turnPromptPaused.fill(false);
        lastBuildingAction.fill(rules::board::SquareType::Count);
        housingShortageOn = false;
        buildingPlacementOn = false;
        configurationAcceptInFlight.fill(false);
        cardSeenInFlight.fill(false);
        auctionOn = false;
        gracePeriodForHumanActivity = {};

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
        processPurchaseContextMessage(message);
        processProfileMessage(state, message);
        processPassiveRuntimeMessage(state, message);
        processTradeAttitudeMessage(state, message);
        (void)trade::processTradeRuleMessage(
            state, message, tradeIngress);
        maybeChooseTradeAcceptance(state, message);
        maybeCounterTrade(state, message);
        processEconomicRuntimeMessage(state, message);
        processFreeUnmortgageMessage(state, message);
        processBuyDecisionMessage(state, message);
        processAuctionMessage(state, message);
        processHousingShortageMessage(state, message);
        processDebtMessage(state, message);
        processTaxDecisionMessage(state, message);
        processJailDecisionMessage(state, message);
        processTurnPromptMessage(state, message);
        // Completion handlers above must release their action flags before a
        // deferred trade decision is allowed to restart the current phase.
        maybeRestartDeferredAcceptance(message);
        maybeProposeAutonomousTrade(state, message);
        maybeSpendAutonomousAssets(state, message);
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
