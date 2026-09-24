#include "UIMessages.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace monopoly::uimsg
{
    namespace
    {
        std::deque<Message> eventQueue;
        std::mutex eventMutex;
        std::condition_variable eventReady;
        bool initialized = false;
    }

    bool initialize()
    {
        {
            const std::scoped_lock lock(eventMutex);
            eventQueue.clear();
            initialized = true;
        }
        eventReady.notify_all();
        return true;
    }

    void shutdown()
    {
        {
            const std::scoped_lock lock(eventMutex);
            eventQueue.clear();
            initialized = false;
        }
        eventReady.notify_all();
    }

    bool send(const Message& message)
    {
        {
            const std::scoped_lock lock(eventMutex);
            if (!initialized)
                return false;

            // L_UIMsg.cpp original protege la file contre les evenements
            // repetitifs non essentiels. Les timers periodiques appliquent la
            // meme regle dans Timers.cpp; MouseMoved est le seul autre type
            // moderne correspondant actuellement a ce contrat.
            if (message.type == Type::MouseMoved &&
                eventQueue.size() > QueueCapacity / 2)
                return false;

            if (eventQueue.size() >= QueueCapacity)
                return false;

            eventQueue.push_back(message);
        }
        eventReady.notify_one();
        return true;
    }

    bool receive(Message& message)
    {
        const std::scoped_lock lock(eventMutex);
        if (!initialized || eventQueue.empty())
            return false;

        message = eventQueue.front();
        eventQueue.pop_front();
        return true;
    }

    bool wait(Message& message)
    {
        std::unique_lock lock(eventMutex);
        eventReady.wait(lock, []
        {
            return !initialized || !eventQueue.empty();
        });
        if (!initialized || eventQueue.empty())
            return false;

        message = eventQueue.front();
        eventQueue.pop_front();
        return true;
    }

    void flushEvents()
    {
        const std::scoped_lock lock(eventMutex);
        eventQueue.clear();
    }

    int percentageFull()
    {
        const std::scoped_lock lock(eventMutex);
        return static_cast<int>(
            (eventQueue.size() * 100U) / QueueCapacity);
    }

    std::size_t size()
    {
        const std::scoped_lock lock(eventMutex);
        return eventQueue.size();
    }

    std::size_t discardTimerEvents(std::size_t timerIndex)
    {
        const std::scoped_lock lock(eventMutex);
        return std::erase_if(eventQueue, [timerIndex](const Message& message)
        {
            return message.type == Type::TimerReachedZero &&
                message.numberA >= 0 &&
                static_cast<std::uint64_t>(message.numberA) == timerIndex;
        });
    }
}
