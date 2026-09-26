#include "Messaging.hpp"

#include <deque>
#include <algorithm>
#include <optional>
#include <array>
#include <utility>

namespace monopoly::messaging
{
    namespace
    {
        std::deque<actions::Message> messageQueue;

        bool currentServerMode = true;
        bool currentNetworkMode = false;

        bool initialized = false;
        std::unique_ptr<Transport> network;
        std::optional<actions::Message> pendingAdmission;
        std::deque<actions::Message> waitingAdmissions;
        std::array<std::uint32_t, rules::MaxPlayers> playerOwners{};
        std::deque<std::uint32_t> disconnectedSources;
        bool gameplaySynchronized{};
        std::function<bool()> networkStarter;
        bool hostDisconnected{};
        bool wasConnected{};
    }

    bool initialize()
    {
        network.reset();
        pendingAdmission.reset();
        waitingAdmissions.clear();
        playerOwners.fill(0);
        disconnectedSources.clear();
        networkStarter = {};
        hostDisconnected = false;
        wasConnected = false;
        // MESS_InitializeSystem() original :
        //
        // MessageQueue.head = -1;
        // MESS_ServerMode = TRUE;
        // MESS_NetworkMode = FALSE;

        messageQueue.clear();

        currentServerMode = true;
        currentNetworkMode = false;

        initialized = true;

        return true;
    }

    void shutdown()
    {
        network.reset();
        pendingAdmission.reset();
        waitingAdmissions.clear();
        messageQueue.clear();

        currentServerMode = true;
        currentNetworkMode = false;

        initialized = false;
    }

    void resetPlayerOwners()
    {
        disconnectedSources.clear();
        for (rules::PlayerNumber player = 0; player < rules::MaxPlayers; ++player)
            setPlayerOwner(player, 0);
    }

    void setPlayerOwner(rules::PlayerNumber player, std::uint32_t source)
    {
        if (player >= rules::MaxPlayers) return;
        playerOwners[player] = source;
        if (network) network->setPlayerOwner(player, source);
    }

    std::uint32_t playerOwner(rules::PlayerNumber player)
    {
        return player < rules::MaxPlayers ? playerOwners[player] : 0;
    }

    bool authenticSender(const rules::GameState& state, const actions::Message& message)
    {
        if (!network || !network->gameplayEnabled()) return true;
        if (message.sourceId != 0 && !network->sourceConnected(message.sourceId))
        {
            // Only the exact terminal packet remains useful after a peer dies.
            // Never admit a queued registration/action for an orphaned source.
            static const std::vector<std::uint8_t> stopPacket{'S', 'T', 'O', 'P', 0, 0, 0, 0};
            return message.action == actions::Type::VoiceChat &&
                message.fromPlayer == rules::SpectatorPlayer &&
                message.toPlayer == rules::BankPlayer && message.binaryDataA == stopPacket;
        }
        if (message.fromPlayer == rules::BankPlayer) return message.sourceId == 0;
        if (message.fromPlayer < state.numberOfPlayers)
            return playerOwner(message.fromPlayer) == message.sourceId;
        return message.fromPlayer == rules::SpectatorPlayer;
    }

    void stopNetwork()
    {
        network.reset();
        pendingAdmission.reset();
        waitingAdmissions.clear();
        disconnectedSources.clear();
        messageQueue.clear();
        playerOwners.fill(0);
        currentNetworkMode = false;
        currentServerMode = true;
        wasConnected = false;
        gameplaySynchronized = false;
    }

    void setNetworkStarter(std::function<bool()> starter)
    {
        networkStarter = std::move(starter);
    }

    bool startConfiguredNetwork()
    {
        if (network && network->gameplayEnabled()) return true;
        stopNetwork();
        return networkStarter && networkStarter();
    }

    bool gameplayNetwork()
    {
        return network && network->gameplayEnabled();
    }

    bool gameplayReady()
    {
        return gameplayNetwork() && currentNetworkMode &&
            (currentServerMode || gameplaySynchronized);
    }

    void noteClientResynchronized()
    {
        if (network && !currentServerMode && network->gameplayEnabled())
            gameplaySynchronized = true;
    }

    bool consumeHostDisconnected()
    {
        return std::exchange(hostDisconnected, false);
    }

    bool startNetwork(std::unique_ptr<Transport> transport)
    {
        if (!initialized || network || !transport)
            return false;
        pendingAdmission.reset();
        currentServerMode = transport->server();
        currentNetworkMode = false;
        if (!currentServerMode)
            messageQueue.clear(); // Discard notifications from local startup.
        network = std::move(transport);
        wasConnected = false;
        gameplaySynchronized = false;
        hostDisconnected = false;
        for (rules::PlayerNumber player = 0; player < rules::MaxPlayers; ++player)
            network->setPlayerOwner(player, playerOwners[player]);
        return true;
    }

    void pumpNetwork()
    {
        if (!initialized || !network)
            return;
        network->pump();
        currentNetworkMode = network->active();
        if (!currentServerMode && network->gameplayEnabled() &&
            !currentNetworkMode && (wasConnected || !network->error().empty()))
        {
            stopNetwork();
            hostDisconnected = true;
            return;
        }
        wasConnected = wasConnected || currentNetworkMode;
        std::uint32_t source = 0;
        while (network->receiveDisconnectedSource(source))
        {
            if (pendingAdmission && pendingAdmission->sourceId == source) pendingAdmission.reset();
            std::erase_if(waitingAdmissions, [source](const auto& admission)
                { return admission.sourceId == source; });
            if (source != 0 && std::find(playerOwners.begin(), playerOwners.end(), source) != playerOwners.end() &&
                std::find(disconnectedSources.begin(), disconnectedSources.end(), source) == disconnectedSources.end())
                disconnectedSources.push_back(source);
        }
        if (!currentNetworkMode)
            pendingAdmission.reset();
        if (!currentServerMode && !currentNetworkMode)
            messageQueue.clear();
        if (!currentNetworkMode)
            std::erase_if(messageQueue, [](const actions::Message& message)
            {
                return message.action == actions::Type::VoiceChat ||
                    message.action == actions::Type::NotifyVoiceChat;
            });
        // Drain the existing local work before delivering admission to RULE.
        // Importing later voice frames here would consume the slots needed by
        // its configuration, AI state, compact state and contract notifications.
        if (pendingAdmission || !disconnectedSources.empty())
            return;
        actions::Message received;
        while (messageQueue.size() < MessageQueueCapacity &&
               network->receive(received))
        {
            if (!currentNetworkMode && (received.action == actions::Type::VoiceChat ||
                received.action == actions::Type::NotifyVoiceChat))
                continue;
            if (currentServerMode && received.sourceId != 0 &&
                received.action == actions::Type::ResyncClient)
            {
                if (currentNetworkMode)
                    pendingAdmission = std::move(received);
                break;
            }
            messageQueue.push_back(std::move(received));
        }
    }

    std::string_view networkError()
    {
        return network ? network->error() : std::string_view{};
    }

    bool sendAction(const actions::Message& message)
    {
        if (!initialized)
        {
            return false;
        }

        if (network && !currentNetworkMode &&
            (message.action == actions::Type::VoiceChat ||
             message.action == actions::Type::NotifyVoiceChat))
            return false;

        if (network && !currentServerMode)
        {
            // The transport assigns the connection identity. RULE on the host
            // authenticates the current player owner before dispatch; no echo.
            if (!network->gameplayEnabled() && message.action != actions::Type::VoiceChat)
                return false;
            return network->send(message);
        }

        if (messageQueue.size() >= MessageQueueCapacity)
        {
            return false;
        }

        if (network && currentNetworkMode &&
            message.fromPlayer == rules::BankPlayer &&
            (message.toPlayer == rules::AllPlayers ||
             (network->gameplayEnabled() && message.toPlayer < rules::MaxPlayers)) &&
            static_cast<unsigned>(message.action) >= 80u &&
            !network->send(message))
        {
            return false;
        }

        auto local = message;
        local.sourceId = 0;
        messageQueue.push_back(std::move(local));

        return true;
    }

    bool sendAction(
        actions::Type action,
        rules::PlayerNumber fromPlayer,
        rules::PlayerNumber toPlayer,
        std::int64_t numberA,
        std::int64_t numberB,
        std::int64_t numberC,
        std::int64_t numberD,
        std::wstring_view stringA)
    {
        actions::Message message{};

        message.action = action;
        message.fromPlayer = fromPlayer;
        message.toPlayer = toPlayer;

        message.numberA = numberA;
        message.numberB = numberB;
        message.numberC = numberC;
        message.numberD = numberD;

        const std::size_t count =
            std::min(
                stringA.size(),
                message.stringA.size() - 1
            );

        std::copy_n(
            stringA.begin(),
            count,
            message.stringA.begin()
        );

        message.stringA[count] = L'\0';

        return sendAction(message);
    }

    void clearActionQueue()
    {
        messageQueue.clear();
        // Admission describes a live connection, so a local game restart must
        // not discard its still-required resync.
    }

    std::size_t currentQueueSize()
    {
        return messageQueue.size();
    }

    bool receiveAction(actions::Message& message)
    {
        pumpNetwork();
        if (!initialized)
            return false;
        // Resolve ownership only when RULE can consume the complete reply batch.
        // Slot indices may have changed through a preceding delete/reorder/load.
        while (messageQueue.empty() && !disconnectedSources.empty())
        {
            const auto source = disconnectedSources.front();
            const auto owner = std::find(playerOwners.begin(), playerOwners.end(), source);
            if (owner == playerOwners.end())
            {
                disconnectedSources.pop_front();
                continue;
            }
            const auto player = static_cast<rules::PlayerNumber>(owner - playerOwners.begin());
            setPlayerOwner(player, 0);
            message = {};
            message.action = actions::Type::DisconnectedPlayer;
            message.fromPlayer = rules::BankPlayer;
            message.toPlayer = rules::BankPlayer;
            message.numberA = player;
            return true;
        }
        if (messageQueue.empty())
        {
            if (!pendingAdmission && !waitingAdmissions.empty())
            {
                pendingAdmission = std::move(waitingAdmissions.front());
                waitingAdmissions.pop_front();
            }
            if (!pendingAdmission) return false;
            // Return directly instead of enqueuing: the complete queue remains
            // available to the synchronous RULE resync batch. Voice-only reads
            // deliberately leave admission pending throughout animation locks.
            message = std::move(*pendingAdmission);
            pendingAdmission.reset();
            if (network && !network->admitSource(message.sourceId)) return false;
            return true;
        }

        message = std::move(messageQueue.front());
        messageQueue.pop_front();
        return true;
    }

    bool receiveVoiceChatOnly(actions::Message& message)
    {
        pumpNetwork();
        if (!initialized)
            return false;

        const auto isVoice = [](const actions::Message& queued)
        {
            return queued.action == actions::Type::VoiceChat ||
                queued.action == actions::Type::NotifyVoiceChat;
        };
        const auto found = std::find_if(messageQueue.begin(), messageQueue.end(), isVoice);
        if (found != messageQueue.end())
        {
            message = std::move(*found);
            messageQueue.erase(found);
            return true;
        }
        if ((!pendingAdmission && disconnectedSources.empty()) || !network || !currentNetworkMode)
            return false;

        // Admission waits for an empty ordinary queue, but an animation lock
        // must not silence established speakers until that queue can drain.
        // Return one voice message directly, leaving room for its RULE echo.
        // Later admissions all request the same AllPlayers refresh, so one
        // pending resync covers them without an unbounded side queue.
        actions::Message received;
        for (unsigned budget = 16; budget > 0 &&
             messageQueue.size() < MessageQueueCapacity && network->receive(received); --budget)
        {
            if (currentServerMode && received.sourceId != 0 &&
                received.action == actions::Type::ResyncClient &&
                received.numberA == rules::AllPlayers)
            {
                // Gameplay peers each require admission; never collapse their
                // identities while the ordinary queue is animation-locked.
                if (network->gameplayEnabled())
                {
                    if (!pendingAdmission)
                    {
                        pendingAdmission = std::move(received);
                        continue;
                    }
                    if (received.sourceId != pendingAdmission->sourceId &&
                        network->sourceConnected(received.sourceId) &&
                        std::none_of(waitingAdmissions.begin(), waitingAdmissions.end(),
                            [&](const auto& queued) { return queued.sourceId == received.sourceId; }) &&
                        waitingAdmissions.size() < MessageQueueCapacity)
                        waitingAdmissions.push_back(std::move(received));
                    continue;
                }
                continue;
            }
            if (isVoice(received))
            {
                message = std::move(received);
                return true;
            }
            messageQueue.push_back(std::move(received));
        }
        return false;
    }

    bool serverMode()
    {
        return currentServerMode;
    }

    bool networkMode()
    {
        return currentNetworkMode;
    }

    std::size_t queuedActionCount()
    {
        return messageQueue.size() + (pendingAdmission ? 1u : 0u) + (network ? network->queued() : 0u);
    }
}


