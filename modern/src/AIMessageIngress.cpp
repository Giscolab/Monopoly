#include "AIMessageIngress.hpp"

#include "LocalPlayers.hpp"
#include "LegacyTextIds.hpp"
#include "Messaging.hpp"

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
            std::array<decision::EconomicActionPlan, rules::MaxPlayers> pendingControlAction{};
            std::array<bool, rules::MaxPlayers> hasPendingControlAction{};
            std::array<bool, rules::MaxPlayers> controlRequestInFlight{};
            std::array<bool, rules::MaxPlayers> actionInFlight{};
            std::array<bool, rules::MaxPlayers> controlReleaseInFlight{};
        };

        EconomicRuntimeState economicRuntime{};

        void updateRuntimeFlags(const actions::Message& message) noexcept
        {
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

        [[nodiscard]] std::array<std::uint32_t, 8> retailRandomKeys() noexcept
        {
            std::array<std::uint32_t, 8> keys{};
            for (auto& key : keys)
                key = static_cast<std::uint32_t>(std::rand());
            return keys;
        }

        [[nodiscard]] rules::PlayerNumber buySellMortgagePlayer(
            const rules::GameState& state) noexcept
        {
            if (state.numberOfPendingPhases == 0)
                return rules::NobodyPlayer;
            const auto& phase = state.phaseStack[0];
            if (phase.phase == rules::GamePhase::BuySellMortgage ||
                phase.phase == rules::GamePhase::DecomposeHotel)
                return phase.fromPlayer;
            return rules::NobodyPlayer;
        }

        [[nodiscard]] bool economicBusy(rules::PlayerNumber player) noexcept
        {
            return player < rules::MaxPlayers &&
                (economicRuntime.hasPendingControlAction[player] ||
                 economicRuntime.controlRequestInFlight[player] ||
                 economicRuntime.actionInFlight[player] ||
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
            const rules::GameState& state,
            rules::PlayerNumber player) noexcept
        {
            if (state.numberOfPendingPhases == 0)
                return false;
            if (state.phaseStack[0].phase == rules::GamePhase::CollectingPayment &&
                state.phaseStack[0].fromPlayer == player)
                return true;
            return state.numberOfPendingPhases >= 2 &&
                state.phaseStack[0].phase == rules::GamePhase::BuySellMortgage &&
                state.phaseStack[1].phase == rules::GamePhase::CollectingPayment &&
                state.phaseStack[1].fromPlayer == player;
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
                         completed == actions::Type::Mortgaging)
                {
                    economicRuntime.actionInFlight[player] = false;
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
                    economicRuntime.controlReleaseInFlight[player] = false;
                return;
            }
            if (message.numberA < 0 || message.numberA >= state.numberOfPlayers)
                return;

            const auto player = static_cast<rules::PlayerNumber>(message.numberA);
            if (!ui::localplayers::slotIsLocalAIPlayer(player) ||
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

            if (state.numberOfPendingPhases != 0)
            {
                const auto phase = state.phaseStack[0].phase;
                if (phase == rules::GamePhase::PlaceBuilding ||
                    phase == rules::GamePhase::HousingShortageQuestion ||
                    phase == rules::GamePhase::CollectingPayment)
                    return true;
            }

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
                    autonomousTradeBlocked(state, player))
                    continue;

                const auto next = nextEconomicDecision(state, player, context);
                if (next.defer || !next.plan.acted())
                    continue;
                if (queueEconomicAction(state, player, next.plan))
                    return;
            }
        }

        [[nodiscard]] decision::SemiImportantTradeInputs makeSemiImportantInputs(
            const rules::GameState& state, rules::PlayerNumber player,
            std::uint8_t importance, const profile::Profile& strategy,
            const profile::ConfigContext& context) noexcept
        {
            decision::SemiImportantTradeInputs inputs{};
            inputs.partnerRoll = retailCounterRoll();
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
            inputs.minimumNonmonopolyTradeAttitude =
                strategy.minimumNonmonopolyTradeAttitude;
            return inputs;
        }

        void maybeProposeAutonomousTrade(
            const rules::GameState& state,
            const actions::Message& message) noexcept
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
                if (!context.localAIPlayer[player] || autonomousTradeBlocked(state, player))
                    continue;

                const auto& strategy = profileRuntime.players[player];
                const bool shouldGiveAway = decision::shouldGiveAwayMonopoly(
                    state, player, strategy.cashStrategy, strategy.minCashOnHand,
                    strategy.maxHousesPerSquareForGiveAway, context.moneyOwed);

                trade::TradeCadenceInputs cadence{};
                cadence.playerSendingTrade =
                    tradeIngress.counterRuntime.sending.state !=
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
                    continue;

                trade::PropertySets properties{};
                for (rules::PlayerNumber owner = 0; owner < state.numberOfPlayers; ++owner)
                    properties[owner] = ai::propertiesOwnedByPlayer(state, owner);

                decision::ProactiveTradeInputs inputs{};
                inputs.shouldGiveAwayMonopoly = shouldGiveAway;
                const auto assets = ai::liquidAssets(
                    state, player, false, false, context.moneyOwed[player]);
                if (state.players[player].aiPlayerLevel == 3)
                {
                    inputs.monopolyGroups = trade::orderMonopolyImportance(
                        assets, trade::MonopolySortOrder::Descending);
                }
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
                    inputs.semiImportant = makeSemiImportantInputs(
                        state, player, 0, strategy, context);
                    proposal = decision::buildProactiveTrade(
                        state, player, strategy.playerAttitude, properties, inputs, config);
                }
                if (proposal.kind == decision::ProactiveTradeKind::None ||
                    proposal.kind == decision::ProactiveTradeKind::NeedsSemiImportant)
                    continue;
                if (trade::sendProactiveTrade(
                        state, player, proposal.proposal, strategy, tradeIngress))
                    return;
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
        economicRuntime = {};
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
        processProfileMessage(state, message);
        processTradeAttitudeMessage(state, message);
        (void)trade::processTradeRuleMessage(
            state, message, tradeIngress);
        maybeRestartDeferredAcceptance(message);
        maybeChooseTradeAcceptance(state, message);
        maybeCounterTrade(state, message);
        processEconomicRuntimeMessage(state, message);
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
