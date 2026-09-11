#include "AIMessageIngress.hpp"
#include "AITradeIngress.hpp"
#include "BoardRules.hpp"
#include "Messaging.hpp"
#include "LegacyTextIds.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;
    bool localRecipient = true;
    bool localAIPlayer = false;
    bool removeRequested = false;
    rules::PlayerNumber removedPlayer = rules::NobodyPlayer;

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

    bool slotIsLocalAIPlayer(rules::PlayerNumber)
    {
        return localAIPlayer;
    }

    bool requestRemoveLocalPlayer(
        const rules::GameState&, rules::PlayerNumber player)
    {
        removeRequested = true;
        removedPlayer = player;
        return true;
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

    void testTradeLifecycleTracking()
    {
        auto state = baseState();
        ai::trade::TradeIngressState ingress{};
        ai::trade::resetTradeIngress(ingress);

        actions::Message started{};
        started.action = actions::Type::NotifyTradeStarted;
        started.numberA = 1;
        require(ai::trade::processTradeRuleMessage(state, started, ingress) &&
                ingress.tradeStarted && ingress.proposedPlayer == 1 &&
                ingress.lastEditor == 1,
            "AI trade ingress tracks retail trade proposer on start");

        actions::Message acceptance{};
        acceptance.action = actions::Type::NotifyTradeAcceptanceDecision;
        require(ai::trade::processTradeRuleMessage(state, acceptance, ingress) &&
                ingress.tradeOfferedForAcceptance &&
                ingress.proposedPlayer == 1,
            "AI trade ingress tracks offer-for-acceptance phase");

        require(ai::trade::processTradeRuleMessage(
                    state, editorMessage(0), ingress) &&
                !ingress.tradeOfferedForAcceptance &&
                ingress.proposedPlayer == 1 && ingress.lastEditor == 0,
            "AI trade ingress clears acceptance phase when editor changes");
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
        ingress.tradeJustRejectedCountered = true;
        ingress.playerJustRejectedCountered = 0;
        completed.numberC = 1;
        (void)ai::trade::processTradeRuleMessage(state, completed, ingress);
        require(ingress.tradeJustRejectedCountered,
            "another player's completion cannot release reject-counter guard");
        completed.numberC = 0;
        completed.numberB = 0;
        auto failed = ingress;
        (void)ai::trade::processTradeRuleMessage(state, completed, failed);
        require(!failed.tradeJustRejectedCountered &&
                failed.counterRuntime.sending.state == ai::trade::SendingTradeState::Nothing,
            "failed StartTradeEditing clears sending state and reject-counter guard");
        completed.numberB = 1;
        (void)ai::trade::processTradeRuleMessage(state, completed, ingress);
        require(!ingress.tradeJustRejectedCountered,
            "successful StartTradeEditing releases reject-counter guard");

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

    std::filesystem::path profileDirectory()
    {
        return std::filesystem::path(MONOPOLY_LEGACY_SOURCE_DIR) /
            "monopoly";
    }

    void testProfileNotificationLifecycle()
    {
        auto state = baseState();
        require(ai::initializeMessageIngressProfiles(
                    profileDirectory()).has_value(),
            "AI message ingress initializes retail profiles");

        actions::Message named{};
        named.action = actions::Type::NotifyNamePlayer;
        named.fromPlayer = rules::BankPlayer;
        named.toPlayer = rules::AllPlayers;
        named.numberA = 0;
        state.players[0].token = 0;
        state.players[0].aiPlayerLevel = 3;
        localRecipient = true;
        localAIPlayer = true;
        ai::processMessage(state, named);
        const auto& loaded = ai::profileRuntimeStateReadOnly();
        require(loaded.playerLoaded[0] && loaded.loadedToken[0] == 0 &&
                loaded.loadedLevel[0] == 3,
            "NotifyNamePlayer loads local AI token profile");

        localAIPlayer = false;
        ai::processMessage(state, named);
        require(!ai::profileRuntimeStateReadOnly().playerLoaded[0],
            "NotifyNamePlayer clears profile when slot is no longer local AI");

        localAIPlayer = true;
        removeRequested = false;
        removedPlayer = rules::NobodyPlayer;
        state.players[0].token = 99;
        state.players[0].aiPlayerLevel = 2;
        ai::processMessage(state, named);
        require(removeRequested && removedPlayer == 0,
            "failed local AI profile load requests retail slot removal");
        ai::resetMessageIngress();
        require(ai::profileRuntimeStateReadOnly().expertLoaded,
            "message ingress reset preserves initialized expert profile");
    }

    void testProfileDrivenCounterFromEditorNotification()
    {
        auto state = baseState();
        state.options.housesPerHotel = 5;
        state.options.evenBuildRule = true;
        state.squares[static_cast<std::size_t>(
            rules::board::SquareType::MediterraneanAvenue)].owner = 1;

        require(messaging::initialize(),
            "profile-driven counter fixture initializes messaging");
        messaging::clearActionQueue();
        require(ai::initializeMessageIngressProfiles(
                    profileDirectory()).has_value(),
            "profile-driven counter loads retail profiles");

        actions::Message named{};
        named.action = actions::Type::NotifyNamePlayer;
        named.toPlayer = rules::AllPlayers;
        named.numberA = 0;
        state.players[0].token = 0;
        state.players[0].aiPlayerLevel = 3;
        localRecipient = true;
        localAIPlayer = true;
        ai::processMessage(state, named);

        actions::Message started{};
        started.action = actions::Type::NotifyTradeStarted;
        started.toPlayer = rules::AllPlayers;
        started.numberA = 1;
        ai::processMessage(state, started);

        const auto property = tradeItemMessage(
            1, 0, rules::TradeItemKind::Square,
            static_cast<std::int64_t>(
                rules::board::SquareType::MediterraneanAvenue));
        ai::processMessage(state, property);
        messaging::clearActionQueue();

        ai::processMessage(state, editorMessage(0));
        const auto& ingress = ai::tradeIngressStateReadOnly();
        require(ingress.counterSessions[0].timesCounteredTrade == 1 &&
                ingress.counterRuntime.sending.state !=
                    ai::trade::SendingTradeState::Nothing,
            "NotifyTradeEditor runs retail-profile counter proposal");
        require(messaging::currentQueueSize() > 0,
            "profile-driven counter emits rule actions");

        messaging::shutdown();
        ai::resetMessageIngress();
        localAIPlayer = false;
    }

    void testTradeAcceptanceDecisionCore()
    {
        auto state = baseState();
        ai::trade::TradeIngressState ingress{};
        ai::trade::TradeAcceptanceConfig config{};
        ingress.proposedPlayer = 1;

        auto result = ai::trade::evaluateCurrentTradeAcceptance(
            state, 0, config, ingress);
        require(result.status == ai::trade::TradeAcceptanceStatus::AcceptUninvolved,
            "AI trade acceptance auto-accepts a requested non-participant");

        ingress.pendingTradeAcceptPlayers = 1u;
        result = ai::trade::evaluateCurrentTradeAcceptance(state, 0, config, ingress);
        require(result.status == ai::trade::TradeAcceptanceStatus::Busy,
            "AI trade acceptance suppresses duplicate pending accepts");
        ingress.pendingTradeAcceptPlayers = 0;

        ingress.tradeJustRejectedCountered = true;
        result = ai::trade::evaluateCurrentTradeAcceptance(state, 0, config, ingress);
        require(result.status == ai::trade::TradeAcceptanceStatus::Suppressed,
            "AI trade acceptance suppresses peers after reject or counter");
        ingress.tradeJustRejectedCountered = false;

        ingress.currentTrade[0].cashReceived = 100;
        ingress.currentTrade[1].cashGiven = 100;
        config.minEvaluationThreshold = -1000000.0;
        config.minEvaluationIfFedUp = -1000000.0;
        result = ai::trade::evaluateCurrentTradeAcceptance(state, 0, config, ingress);
        require(result.status == ai::trade::TradeAcceptanceStatus::Accept,
            "AI trade acceptance accepts an involved trade above threshold");

        config.minEvaluationThreshold = 1000000.0;
        result = ai::trade::evaluateCurrentTradeAcceptance(state, 0, config, ingress);
        require(result.status == ai::trade::TradeAcceptanceStatus::CounterOrReject,
            "AI trade acceptance requests counter path below normal threshold");

        ingress.immunities[0].count = 1;
        ingress.immunities[0].fromPlayer = 0;
        ingress.immunities[0].toPlayer = 1;
        result = ai::trade::evaluateCurrentTradeAcceptance(state, 0, config, ingress);
        require(result.status == ai::trade::TradeAcceptanceStatus::RejectFutureOrImmunity,
            "AI trade acceptance rejects futures or immunities before evaluation");
        ingress.immunities = {};

        const auto property = rules::board::propertyBit(
            rules::board::SquareType::MediterraneanAvenue);
        ingress.currentTrade = {};
        ingress.currentTrade[0].propertiesReceived = property;
        ingress.currentTrade[1].propertiesGiven = property;
        ingress.counterSessions[0].propertyMemory.bit1[1] = property;
        config.numberTimesAllowPropertyTrade = 1;
        config.minEvaluationIfFedUp = 1000000.0;
        result = ai::trade::evaluateCurrentTradeAcceptance(state, 0, config, ingress);
        require(result.status == ai::trade::TradeAcceptanceStatus::RejectFedUp && result.fedUp,
            "AI trade acceptance applies fed-up threshold to repeated properties");
        config.minEvaluationIfFedUp = -1000000.0;
        result = ai::trade::evaluateCurrentTradeAcceptance(state, 0, config, ingress);
        require(result.status == ai::trade::TradeAcceptanceStatus::Accept && result.fedUp,
            "fed-up trade accepts using its own threshold rather than normal threshold");
        ingress.counterSessions[0].propertyMemory = {};
        ingress.counterSessions[1].propertyMemory.bit1[0] = property;
        result = ai::trade::evaluateCurrentTradeAcceptance(state, 0, config, ingress);
        require(!result.fedUp && result.status == ai::trade::TradeAcceptanceStatus::CounterOrReject,
            "acceptance reads only the deciding player's proposer-specific memory");
    }

    void testAcceptanceNotificationRuntime()
    {
        auto state = baseState();
        require(messaging::initialize(),
            "trade acceptance runtime fixture initializes messaging");
        messaging::clearActionQueue();
        require(ai::initializeMessageIngressProfiles(profileDirectory()).has_value(),
            "trade acceptance runtime loads retail profiles");

        actions::Message named{};
        named.action = actions::Type::NotifyNamePlayer;
        named.toPlayer = rules::AllPlayers;
        named.numberA = 0;
        state.players[0].token = 0;
        state.players[0].aiPlayerLevel = 3;
        localRecipient = true;
        localAIPlayer = true;
        ai::processMessage(state, named);

        actions::Message started{};
        started.action = actions::Type::NotifyTradeStarted;
        started.toPlayer = rules::AllPlayers;
        started.numberA = 1;
        ai::processMessage(state, started);
        messaging::clearActionQueue();

        actions::Message acceptance{};
        acceptance.action = actions::Type::NotifyTradeAcceptanceDecision;
        acceptance.toPlayer = rules::AllPlayers;
        acceptance.numberA = 1u << 0;
        ai::processMessage(state, acceptance);
        const auto firstCount = messaging::currentQueueSize();
        ai::processMessage(state, acceptance);
        require(firstCount == 1 && messaging::currentQueueSize() == 1,
            "NotifyTradeAcceptanceDecision does not duplicate pending AI accept");

        actions::Message sent{};
        require(messaging::receiveAction(sent) &&
                sent.action == actions::Type::TradeAccept &&
                sent.fromPlayer == 0 && sent.numberA == 1 && sent.numberB == 3,
            "non-participant AI emits retail TradeAccept true status 3");


        actions::Message completed{};
        completed.action = actions::Type::NotifyActionCompleted;
        completed.toPlayer = rules::AllPlayers;
        completed.numberA = static_cast<std::int64_t>(actions::Type::TradeAccept);
        completed.numberB = 1;
        completed.numberC = 0;
        ai::processMessage(state, completed);
        require((ai::tradeIngressStateReadOnly().pendingTradeAcceptPlayers & 1u) == 0,
            "TradeAccept completion releases pending AI acceptance guard");
        acceptance.numberA = 1u << 1;
        acceptance.numberB = 1u << 0;
        ai::processMessage(state, acceptance);
        require(messaging::currentQueueSize() == 0,
            "requested unloaded AI is silent and involvedSet is not mistaken for playerSet");
        acceptance.numberA = 1u << 0;
        localAIPlayer = false;
        ai::processMessage(state, acceptance);
        require(messaging::currentQueueSize() == 0,
            "loaded non-local AI does not answer acceptance");
        localAIPlayer = true;
        ai::processMessage(state, tradeItemMessage(1, 0, rules::TradeItemKind::Cash, 100));
        ai::processMessage(state, acceptance);
        require(messaging::receiveAction(sent) && sent.action == actions::Type::TradeAccept &&
                sent.numberA == 1 && sent.numberB == 1 && messaging::currentQueueSize() == 0,
            "involved AI accepts a gift with retail true status 1");
        ai::processMessage(state, completed);
        ai::processMessage(state, started);
        ai::processMessage(state, tradeItemMessage(0, 1, rules::TradeItemKind::Cash, 100));
        actions::Message auction{};
        auction.action = actions::Type::NotifyNewHighBid;
        auction.toPlayer = rules::AllPlayers;
        ai::processMessage(state, auction);
        ai::processMessage(state, acceptance);
        ai::processMessage(state, acceptance);
        require(messaging::receiveAction(sent) && sent.action == actions::Type::TradeAccept &&
                sent.numberA == 0 && sent.numberB == 0 && messaging::currentQueueSize() == 0 &&
                ai::tradeIngressStateReadOnly().tradeJustRejectedCountered,
            "failed counter emits one rejection and suppresses duplicate notifications");
        ai::processMessage(state, completed);
        require(!ai::tradeIngressStateReadOnly().tradeJustRejectedCountered,
            "reject completion releases the global reject-counter guard");

        messaging::shutdown();
        ai::resetMessageIngress();
        localAIPlayer = false;
    }

    void testTradeResponseAttitudes()
    {
        auto state = baseState();
        state.numberOfPlayers = 4;
        state.players[2].cash = 500;
        state.players[3].cash = 500;
        ai::trade::TradeIngressState ingress{};
        ai::profile::ProfileSet profiles{};
        ai::profile::ConfigContext context{};
        context.localAIPlayer = {true, false, true, false};
        for (auto& p : profiles)
        {
            p.cashFactor = 1.0;
            p.chancesThreshold = 1000000.0;
            p.attitudeLostForRejectedTrade = 10.0;
            p.attitudeChangeTradeObserving = 1.0;
            p.neutralAttitude = 50.0;
        }
        profiles[2].attitudeLostForRejectedTrade = 7.0;
        ingress.currentTrade[0].cashGiven = 100;
        ingress.currentTrade[1].cashReceived = 100;
        auto changes = ai::trade::tradeResponseAttitudeChanges(
            state, 0, 1, false, ingress, profiles, context);
        require(changes[0] == -20.0 && changes[2] == -7.0 &&
                changes[1] == 0.0 && changes[3] == 0.0,
            "rejected good trade stacks proposer penalty and capped local observer penalty");
        changes = ai::trade::tradeResponseAttitudeChanges(
            state, 0, 1, true, ingress, profiles, context);
        require(changes[0] == 10.0 && changes[2] == 0.0,
            "accepted good trade rewards proposer personally without observer penalty");

        ingress.currentTrade = {};
        ingress.currentTrade[1].cashGiven = 100;
        ingress.currentTrade[0].cashReceived = 100;
        changes = ai::trade::tradeResponseAttitudeChanges(
            state, 0, 1, true, ingress, profiles, context);
        require(changes[0] == 20.0 && changes[2] == 7.0,
            "accepted bad trade stacks personal reward and capped positive observer change");
        changes = ai::trade::tradeResponseAttitudeChanges(
            state, 0, 1, false, ingress, profiles, context);
        require(changes[0] == -10.0 && changes[2] == 0.0,
            "rejected bad trade only changes proposer personal attitude");

        ingress.currentTrade = {};
        ingress.currentTrade[0].cashGiven = 2;
        ingress.currentTrade[1].cashReceived = 2;
        profiles[2].cashFactor = 0.5;
        profiles[2].attitudeChangeTradeObserving = 0.25;
        changes = ai::trade::tradeResponseAttitudeChanges(
            state, 0, 1, false, ingress, profiles, context);
        require(changes[0] == -12.0 && changes[2] == -0.25,
            "observer uses its own strategy and preserves sub-cap fractional change");
        ingress.immunities[0].count = 1;
        ingress.immunities[0].fromPlayer = 1;
        ingress.immunities[0].toPlayer = 0;
        changes = ai::trade::tradeResponseAttitudeChanges(
            state, 0, 1, false, ingress, profiles, context);
        require(changes[0] == -10.0 && changes[2] == 0.0,
            "futures-immunities skip observation but retain personal rejection");
        changes = ai::trade::tradeResponseAttitudeChanges(
            state, 0, 1, true, ingress, profiles, context);
        require(changes[0] == 10.0 && changes[2] == 0.0,
            "futures-immunities retain personal acceptance");
        context.localAIPlayer[0] = false;
        changes = ai::trade::tradeResponseAttitudeChanges(
            state, 0, 1, false, ingress, profiles, context);
        require(changes == std::array<double, rules::MaxPlayers>{},
            "nonlocal proposer and observers excluded by immunity remain unchanged");
        changes = ai::trade::tradeResponseAttitudeChanges(
            state, rules::NobodyPlayer, 1, false, ingress, profiles, context);
        require(changes == std::array<double, rules::MaxPlayers>{},
            "invalid proposer produces no attitude changes");
    }

    void testTradeAttitudeMessageRouting()
    {
        auto state = baseState();
        require(messaging::initialize(), "attitude routing initializes messaging");
        require(ai::initializeMessageIngressProfiles(profileDirectory()).has_value(),
            "attitude routing loads profiles");
        localAIPlayer = true;
        localRecipient = true;
        actions::Message named{};
        named.action = actions::Type::NotifyNamePlayer;
        named.toPlayer = rules::AllPlayers;
        state.players[0].token = 0;
        state.players[0].aiPlayerLevel = 3;
        ai::processMessage(state, named);
        actions::Message started{};
        started.action = actions::Type::NotifyTradeStarted;
        started.toPlayer = rules::AllPlayers;
        started.numberA = 0;
        ai::processMessage(state, started);
        const auto initial = ai::profileRuntimeStateReadOnly().players[0].playerAttitude[1];
        const auto loss = ai::profileRuntimeStateReadOnly().players[0].attitudeLostForRejectedTrade;
        actions::Message response{};
        response.action = actions::Type::NotifyErrorMessage;
        response.toPlayer = rules::AllPlayers;
        response.numberA = legacy_text::ErrorTradeAccepted;
        response.numberC = 1;
        ai::processMessage(state, response);
        require(ai::profileRuntimeStateReadOnly().players[0].playerAttitude[1] == initial + loss,
            "RULE NotifyErrorMessage acceptance updates proposer attitude");
        response.numberA = legacy_text::ErrorTradeRejected;
        ai::processMessage(state, response);
        require(std::abs(ai::profileRuntimeStateReadOnly().players[0].playerAttitude[1] - initial) < 1e-12,
            "RULE rejection applies signed personal loss");
        response.action = actions::Type::NotifyTextChat;
        ai::processMessage(state, response);
        require(std::abs(ai::profileRuntimeStateReadOnly().players[0].playerAttitude[1] - initial) < 1e-12,
            "TextChat does not masquerade as a trade response");

        localAIPlayer = false;
        actions::Message offered{};
        offered.action = actions::Type::NotifyTradeAcceptanceDecision;
        offered.toPlayer = rules::AllPlayers;
        ai::processMessage(state, offered);
        localAIPlayer = true;
        ai::processMessage(state, editorMessage(1));
        require(std::abs(ai::profileRuntimeStateReadOnly().players[0].playerAttitude[1] - (initial - loss)) < 1e-12,
            "editor counter counts rejection before ingress overwrites previous editor");
        ai::processMessage(state, editorMessage(1));
        require(std::abs(ai::profileRuntimeStateReadOnly().players[0].playerAttitude[1] - (initial - loss)) < 1e-12,
            "editor message outside acceptance does not repeat attitude change");
        response.action = actions::Type::NotifyErrorMessage;
        response.numberA = legacy_text::ErrorTradeChanging;
        ai::processMessage(state, response);
        require(ai::tradeIngressStateReadOnly().proposedPlayer == 1,
            "trade-changing RULE message promotes last editor to proposer");
        const auto beforeTurn = ai::profileRuntimeStateReadOnly().players[0].playerAttitude[1];
        const auto& strategy = ai::profileRuntimeStateReadOnly().players[0];
        const auto expected = beforeTurn > strategy.neutralAttitude
            ? beforeTurn - strategy.attitudeTickChange
            : beforeTurn + strategy.attitudeTickChange;
        const auto expectedCounter = strategy.turnsToForgetPropertyTrade <= 1 ? 0 : 1;
        actions::Message turn{};
        turn.action = actions::Type::NotifyStartTurn;
        turn.toPlayer = rules::AllPlayers;
        turn.numberA = 1;
        ai::processMessage(state, turn);
        require(std::abs(ai::profileRuntimeStateReadOnly().players[0].playerAttitude[1] - expected) < 1e-12 &&
                ai::tradeIngressStateReadOnly().turnState[0].turnsAfterForgettingLast == expectedCounter &&
                ai::tradeIngressStateReadOnly().turnState[1].turnsAfterForgettingLast == 0,
            "every RULE start-turn maintains loaded local AI, including another player's turn");

        messaging::shutdown();
        ai::resetMessageIngress();
        localAIPlayer = false;
    }

    void testPublicAndPrivateAcceptance()
    {
        auto state = baseState();
        state.numberOfPlayers = 3;
        state.players[2].cash = 500;
        require(messaging::initialize(), "multi-AI acceptance initializes messaging");
        require(ai::initializeMessageIngressProfiles(profileDirectory()).has_value(),
            "multi-AI acceptance loads profiles");
        localAIPlayer = true;
        localRecipient = true;
        actions::Message named{};
        named.action = actions::Type::NotifyNamePlayer;
        named.toPlayer = rules::AllPlayers;
        for (rules::PlayerNumber player = 0; player < 2; ++player)
        {
            named.numberA = player;
            state.players[player].token = player;
            state.players[player].aiPlayerLevel = 3;
            ai::processMessage(state, named);
        }
        actions::Message started{};
        started.action = actions::Type::NotifyTradeStarted;
        started.toPlayer = rules::AllPlayers;
        started.numberA = 2;
        ai::processMessage(state, started);
        ai::processMessage(state, tradeItemMessage(0, 2, rules::TradeItemKind::Cash, 100));
        actions::Message auction{};
        auction.action = actions::Type::NotifyNewHighBid;
        auction.toPlayer = rules::AllPlayers;
        ai::processMessage(state, auction);
        actions::Message offer{};
        offer.action = actions::Type::NotifyTradeAcceptanceDecision;
        offer.toPlayer = rules::AllPlayers;
        offer.numberA = 3;
        offer.numberB = 5;
        messaging::clearActionQueue();
        ai::processMessage(state, offer);
        ai::processMessage(state, offer);
        actions::Message sent{};
        require(messaging::receiveAction(sent) && sent.fromPlayer == 0 &&
                sent.action == actions::Type::TradeAccept && sent.numberA == 0 &&
                messaging::currentQueueSize() == 0,
            "public offer first AI rejection blocks peer and duplicate answers");
        actions::Message completed{};
        completed.action = actions::Type::NotifyActionCompleted;
        completed.toPlayer = rules::AllPlayers;
        completed.numberA = static_cast<std::int64_t>(actions::Type::TradeAccept);
        completed.numberB = 1;
        completed.numberC = 0;
        ai::processMessage(state, completed);
        offer.numberA = 2;
        ai::processMessage(state, offer);
        require(messaging::receiveAction(sent) && sent.fromPlayer == 1 &&
                sent.action == actions::Type::TradeAccept && sent.numberA == 1 &&
                sent.numberB == 3 && messaging::currentQueueSize() == 0,
            "completed rejection lets requested uninvolved peer answer true/3");
        ai::processMessage(state, started);
        ai::processMessage(state, tradeItemMessage(2, 0, rules::TradeItemKind::Cash, 100));
        offer.numberA = 1;
        ai::processMessage(state, offer);
        require(messaging::receiveAction(sent) && sent.fromPlayer == 0 &&
                sent.action == actions::Type::TradeAccept && sent.numberA == 1 &&
                sent.numberB == 1 && messaging::currentQueueSize() == 0,
            "private participant mask leaves loaded exterior AI silent");
        messaging::shutdown();
        ai::resetMessageIngress();
        localAIPlayer = false;
    }

    void testAcceptanceCounterRuntime()
    {
        auto state = baseState();
        state.options.housesPerHotel = 5;
        state.options.evenBuildRule = true;
        state.squares[static_cast<std::size_t>(
            rules::board::SquareType::MediterraneanAvenue)].owner = 0;
        require(messaging::initialize(), "acceptance counter initializes messaging");
        require(ai::initializeMessageIngressProfiles(profileDirectory()).has_value(),
            "acceptance counter loads profiles");
        localAIPlayer = true;
        localRecipient = true;
        actions::Message named{};
        named.action = actions::Type::NotifyNamePlayer;
        named.toPlayer = rules::AllPlayers;
        state.players[0].token = 0;
        state.players[0].aiPlayerLevel = 3;
        ai::processMessage(state, named);
        actions::Message started{};
        started.action = actions::Type::NotifyTradeStarted;
        started.toPlayer = rules::AllPlayers;
        started.numberA = 1;
        ai::processMessage(state, started);
        ai::processMessage(state, tradeItemMessage(
            0, 1, rules::TradeItemKind::Square,
            static_cast<std::int64_t>(rules::board::SquareType::MediterraneanAvenue)));
        const auto probability = ai::profileRuntimeStateReadOnly().players[0].tradeCounterProbability;
        unsigned seed = 0;
        for (; seed < 10000; ++seed)
        {
            std::srand(seed);
            if (static_cast<double>(std::rand()) / RAND_MAX <= probability)
                break;
        }
        require(seed < 10000, "counter fixture selects reproducible accepted probability roll");
        std::srand(seed);
        actions::Message offer{};
        offer.action = actions::Type::NotifyTradeAcceptanceDecision;
        offer.toPlayer = rules::AllPlayers;
        offer.numberA = 1;
        offer.numberB = 3;
        messaging::clearActionQueue();
        ai::processMessage(state, offer);
        actions::Message sent{};
        require(messaging::receiveAction(sent) &&
                sent.action == actions::Type::StartTradeEditing &&
                ai::tradeIngressStateReadOnly().counterSessions[0].timesCounteredTrade == 1 &&
                messaging::currentQueueSize() == 0,
            "successful acceptance counter emits editor request without separate rejection");
        ai::processMessage(state, offer);
        require(messaging::currentQueueSize() == 0,
            "counter guard suppresses repeated acceptance notification");
        actions::Message completed{};
        completed.action = actions::Type::NotifyActionCompleted;
        completed.toPlayer = rules::AllPlayers;
        completed.numberA = static_cast<std::int64_t>(actions::Type::StartTradeEditing);
        completed.numberB = 1;
        completed.numberC = 0;
        ai::processMessage(state, completed);
        ai::processMessage(state, editorMessage(0));
        bool sawItems = false;
        bool sawDone = false;
        while (messaging::receiveAction(sent))
        {
            require(sent.action != actions::Type::TradeAccept && !sawDone,
                "counter item stream contains no reject and finalization is last");
            if (sent.action == actions::Type::TradeItem) sawItems = true;
            if (sent.action == actions::Type::TradeEditingDone) sawDone = true;
        }
        require(sawItems && sawDone, "counter resumes stored items and finalization after editor grant");
        ai::processMessage(state, offer);
        require(ai::tradeIngressStateReadOnly().deferredAcceptancePlayers == 1 &&
                messaging::currentQueueSize() == 0,
            "busy counter remembers deferred restart without duplicate acceptance");
        completed.numberA = static_cast<std::int64_t>(actions::Type::TradeEditingDone);
        ai::processMessage(state, completed);
        require(messaging::receiveAction(sent) && sent.action == actions::Type::RestartPhase &&
                sent.fromPlayer == 0 && messaging::currentQueueSize() == 0 &&
                ai::tradeIngressStateReadOnly().deferredAcceptancePlayers == 0,
            "counter completion sends and clears the deferred retail RestartPhase");
        messaging::shutdown();
        ai::resetMessageIngress();
        localAIPlayer = false;
    }

    void testTradeStartTurnMaintenance()
    {
        ai::trade::TradeIngressState ingress{};
        ai::profile::ProfileSet profiles{};
        std::array<bool, rules::MaxPlayers> local{};
        local[0] = true;
        profiles[0].turnsToForgetPropertyTrade = 2;
        profiles[0].neutralAttitude = 0.5;
        profiles[0].attitudeTickChange = 0.25;
        profiles[0].playerAttitude = {0.75, 0.25, 0.5, 0.6};
        ingress.turnState[0].timeLastTrade[0] = 2;
        ingress.turnState[0].timeLastTrade[1] = 0;
        ingress.turnState[0].timeLastTrade[2] = -1;
        ingress.turnState[0].timeLastTrade.back() = 3;
        ingress.counterSessions[0].propertyMemory.bit1[1] = 1;
        ingress.counterSessions[0].propertyMemory.bit2[2] = 2;
        ingress.counterSessions[1].propertyMemory.bit1[0] = 1;
        ai::trade::advanceTradeTurn(2, profiles, local, ingress);
        require(ingress.turnState[0].turnsAfterForgettingLast == 1 &&
                ingress.counterSessions[0].propertyMemory.bit1[1] == 1,
            "start turn retains property memory before configured forgetting interval");
        require(ingress.turnState[0].timeLastTrade[0] == 1 &&
                ingress.turnState[0].timeLastTrade[1] == 0 &&
                ingress.turnState[0].timeLastTrade[2] == -1 &&
                ingress.turnState[0].timeLastTrade.back() == 2,
            "start turn decrements every positive timer only, including last retail slot");
        require(profiles[0].playerAttitude[0] == 0.5 &&
                profiles[0].playerAttitude[1] == 0.5 &&
                profiles[0].playerAttitude[2] == 0.75 &&
                std::abs(profiles[0].playerAttitude[3] - 0.35) < 1e-12,
            "attitude drift preserves retail equality and overshoot quirks without clamp");
        require(ingress.turnState[1].turnsAfterForgettingLast == 0 &&
                ingress.counterSessions[1].propertyMemory.bit1[0] == 1 &&
                profiles[1].playerAttitude[0] == 0.0,
            "start turn leaves nonlocal AI state unchanged");
        ai::trade::advanceTradeTurn(2, profiles, local, ingress);
        require(ingress.turnState[0].turnsAfterForgettingLast == 0 &&
                ingress.counterSessions[0].propertyMemory.bit1[1] == 0 &&
                ingress.counterSessions[0].propertyMemory.bit1[2] == 2 &&
                ingress.counterSessions[0].propertyMemory.bit2[2] == 0,
            "forget interval invokes existing retail bit-plane memory decay");
        actions::Message finished{};
        finished.action = actions::Type::NotifyTradeFinished;
        auto game = baseState();
        ingress.turnState[0].turnsAfterForgettingLast = 1;
        (void)ai::trade::processTradeRuleMessage(game, finished, ingress);
        require(ingress.turnState[0].turnsAfterForgettingLast == 1 &&
                ingress.turnState[0].timeLastTrade.back() == 1 &&
                ingress.counterSessions[0].propertyMemory.bit1[2] == 2,
            "trade finish preserves turn cadence and decaying property memory");
        profiles[0].turnsToForgetPropertyTrade = 0;
        ai::trade::advanceTradeTurn(2, profiles, local, ingress);
        require(ingress.turnState[0].turnsAfterForgettingLast == 0 &&
                ingress.counterSessions[0].propertyMemory.bit1[2] == 0,
            "zero forgetting interval still forgets every turn like retail");
    }

    void testProactiveSendHistory()
    {
        auto game = baseState();
        game.tradeInProgress = false;
        game.squares[static_cast<std::size_t>(
            rules::board::SquareType::MediterraneanAvenue)].owner = 0;
        ai::trade::TradeProposalList proposal{};
        const auto property = rules::board::propertyBit(
            rules::board::SquareType::MediterraneanAvenue);
        proposal[0].propertiesGiven = proposal[1].propertiesReceived = property;
        proposal[0].cashReceived = proposal[1].cashGiven = 50;
        ai::profile::Profile strategy{};
        strategy.maxTrades = 2;
        strategy.timeForgetTrade = 7;
        ai::trade::TradeIngressState ingress{};
        ingress.turnState[0].timeLastTrade[0] = 2;
        require(messaging::initialize(), "proactive history initializes messaging");
        messaging::clearActionQueue();
        require(ai::trade::sendProactiveTrade(game, 0, proposal, strategy, ingress) &&
                ingress.turnState[0].timeLastTrade[0] == 2 &&
                ingress.turnState[0].timeLastTrade[1] == 7 &&
                ingress.counterSessions[0].timesCounteredTrade == 0,
            "proactive send consumes first free configured slot without counting a counter");
        require(!ai::trade::sendProactiveTrade(game, 0, proposal, strategy, ingress) &&
                messaging::currentQueueSize() == 1,
            "busy proactive sender neither duplicates request nor consumes another slot");
        actions::Message message{};
        require(messaging::receiveAction(message) &&
                message.action == actions::Type::StartTradeEditing,
            "new proactive offer first requests RULE editing permission");
        const auto pending = ingress;
        actions::Message started{};
        started.action = actions::Type::NotifyTradeStarted;
        started.numberA = 0;
        actions::Message completed{};
        completed.action = actions::Type::NotifyActionCompleted;
        completed.numberA = static_cast<std::int64_t>(actions::Type::StartTradeEditing);
        completed.numberB = 1;
        completed.numberC = 0;
        auto completedFirst = pending;
        (void)ai::trade::processTradeRuleMessage(game, completed, completedFirst);
        (void)ai::trade::processTradeRuleMessage(game, started, completedFirst);
        require(completedFirst.counterRuntime.hasPendingProposal &&
                completedFirst.counterRuntime.pendingProposal == proposal,
            "trade synchronization retains own proposal after completed edit grant");
        (void)ai::trade::processTradeRuleMessage(game, started, ingress);
        require(ingress.counterRuntime.hasPendingProposal &&
                ingress.counterRuntime.pendingProposal == proposal,
            "trade synchronization retains own proposal before edit grant completes");
        (void)ai::trade::processTradeRuleMessage(game, completed, ingress);
        game.tradeInProgress = true;
        (void)ai::trade::processTradeRuleMessage(game, editorMessage(0), ingress);
        require(messaging::receiveAction(message) &&
                message.action == actions::Type::TradeItem &&
                message.numberC == static_cast<std::int64_t>(rules::TradeItemKind::Cash) &&
                message.numberD == 50,
            "proactive continuation sends cash first");
        require(messaging::receiveAction(message) &&
                message.action == actions::Type::TradeItem &&
                message.numberC == static_cast<std::int64_t>(rules::TradeItemKind::Square),
            "proactive continuation sends property after cash");
        require(messaging::receiveAction(message) &&
                message.action == actions::Type::TradeEditingDone &&
                message.numberA == 1 && messaging::currentQueueSize() == 0,
            "proactive continuation finalizes as offer rather than acceptance counter");
        completed.numberA = static_cast<std::int64_t>(actions::Type::TradeEditingDone);
        (void)ai::trade::processTradeRuleMessage(game, completed, ingress);
        require(!ingress.counterRuntime.hasPendingProposal &&
                ingress.turnState[0].timeLastTrade[1] == 7,
            "proactive finalization releases sending state and retains history cooldown");
        auto denied = pending;
        completed.numberA = static_cast<std::int64_t>(actions::Type::StartTradeEditing);
        completed.numberB = 0;
        (void)ai::trade::processTradeRuleMessage(game, completed, denied);
        require(denied.counterRuntime.sending.state == ai::trade::SendingTradeState::Nothing &&
                denied.turnState[0].timeLastTrade[1] == 7,
            "RULE-denied edit still retains cooldown recorded when request was queued");
        auto foreign = pending;
        started.numberA = 1;
        (void)ai::trade::processTradeRuleMessage(game, started, foreign);
        require(!foreign.counterRuntime.hasPendingProposal,
            "unrelated player's trade synchronization discards stale pending offer");

        game.tradeInProgress = false;
        ingress = {};
        ingress.turnState[0].timeLastTrade[0] = -1;
        ingress.turnState[0].timeLastTrade[1] = 1;
        require(!ai::trade::sendProactiveTrade(game, 0, proposal, strategy, ingress) &&
                messaging::currentQueueSize() == 0,
            "proactive history requires exactly zero slot inside configured maxTrades");
        ingress = {};
        for (std::size_t index = 0; index < messaging::MessageQueueCapacity; ++index)
            (void)messaging::sendAction(actions::Type::RestartPhase, 0, rules::BankPlayer);
        require(!ai::trade::sendProactiveTrade(game, 0, proposal, strategy, ingress) &&
                ingress.turnState[0].timeLastTrade[0] == 0 &&
                !ingress.counterRuntime.hasPendingProposal,
            "full queue publishes neither pending proactive offer nor cooldown");
        messaging::clearActionQueue();
        auto invalid = proposal;
        invalid[1].propertiesReceived = 0;
        require(!ai::trade::sendProactiveTrade(game, 0, invalid, strategy, ingress) &&
                ingress.turnState[0].timeLastTrade[0] == 0 &&
                messaging::currentQueueSize() == 0,
            "invalid proactive offer does not consume history or emit RULE action");
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
        testTradeLifecycleTracking();
        testCounterSendContinuation();
        testProfileNotificationLifecycle();
        testProfileDrivenCounterFromEditorNotification();
        testTradeAcceptanceDecisionCore();
        testAcceptanceNotificationRuntime();
        testTradeResponseAttitudes();
        testTradeAttitudeMessageRouting();
        testPublicAndPrivateAcceptance();
        testAcceptanceCounterRuntime();
        testTradeStartTurnMaintenance();
        testProactiveSendHistory();
        testTradeFinishAndMessageFilter();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
