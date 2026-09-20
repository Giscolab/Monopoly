#include "Messaging.hpp"

#include <deque>
#include <algorithm>
#include <optional>

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
    }

    bool initialize()
    {
        network.reset();
        pendingAdmission.reset();
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
        messageQueue.clear();

        currentServerMode = true;
        currentNetworkMode = false;

        initialized = false;
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
        return true;
    }

    void pumpNetwork()
    {
        if (!initialized || !network)
            return;
        network->pump();
        currentNetworkMode = network->active();
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
        if (pendingAdmission)
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
            // Spectator clients send only validated voice traffic to the bank.
            // No local echo: RULE on the host produces the notification.
            if (message.action != actions::Type::VoiceChat)
                return false;
            return network->send(message);
        }

        if (messageQueue.size() >= MessageQueueCapacity)
        {
            return false;
        }

        if (network && currentNetworkMode &&
            message.fromPlayer == rules::BankPlayer &&
            message.toPlayer == rules::AllPlayers &&
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
        if (messageQueue.empty())
        {
            if (!pendingAdmission)
                return false;
            // Return directly instead of enqueuing: the complete queue remains
            // available to the synchronous RULE resync batch. Voice-only reads
            // deliberately leave admission pending throughout animation locks.
            message = std::move(*pendingAdmission);
            pendingAdmission.reset();
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
        if (!pendingAdmission || !network || !currentNetworkMode)
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
                continue;
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


