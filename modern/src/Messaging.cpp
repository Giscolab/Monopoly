#include "Messaging.hpp"

#include <deque>
#include <algorithm>

namespace monopoly::messaging
{
    namespace
    {
        std::deque<actions::Message> messageQueue;

        bool currentServerMode = true;
        bool currentNetworkMode = false;

        bool initialized = false;
    }

    bool initialize()
    {
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
        messageQueue.clear();

        currentServerMode = true;
        currentNetworkMode = false;

        initialized = false;
    }

    bool sendAction(const actions::Message& message)
    {
        if (!initialized)
        {
            return false;
        }

        if (messageQueue.size() >= MessageQueueCapacity)
        {
            return false;
        }

        messageQueue.push_back(message);

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
    }

    std::size_t currentQueueSize()
    {
        return messageQueue.size();
    }

    bool receiveAction(actions::Message& message)
    {
        if (!initialized || messageQueue.empty())
        {
            return false;
        }

        message = std::move(messageQueue.front());
        messageQueue.pop_front();

        return true;
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
        return messageQueue.size();
    }
}


