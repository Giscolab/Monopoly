#include "UIMessages.hpp"

#include <deque>

namespace monopoly::uimsg
{
    namespace
    {
        std::deque<Message> eventQueue;
        bool initialized = false;
    }

    bool initialize()
    {
        eventQueue.clear();
        initialized = true;
        return true;
    }

    void shutdown()
    {
        eventQueue.clear();
        initialized = false;
    }

    bool send(const Message& message)
    {
        if (!initialized)
        {
            return false;
        }

        // L_UIMsg.cpp original protege la file contre les evenements
        // repetitifs non essentiels. Les timers periodiques appliquent la
        // meme regle dans Timers.cpp; MouseMoved est le seul autre type
        // moderne correspondant actuellement a ce contrat.
        if (message.type == Type::MouseMoved &&
            eventQueue.size() > QueueCapacity / 2)
        {
            return false;
        }

        if (eventQueue.size() >= QueueCapacity)
        {
            return false;
        }

        eventQueue.push_back(message);
        return true;
    }

    bool receive(Message& message)
    {
        if (!initialized || eventQueue.empty())
        {
            return false;
        }

        message = eventQueue.front();
        eventQueue.pop_front();

        return true;
    }

    std::size_t size()
    {
        return eventQueue.size();
    }
}
