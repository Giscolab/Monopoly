#include "AIMessageIngress.hpp"
#include "AITradeIngress.hpp"
#include "BoardRules.hpp"
#include "Messaging.hpp"

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
        testTradeFinishAndMessageFilter();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
