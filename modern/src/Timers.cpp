#include "Timers.hpp"

#include "UIMessages.hpp"

#include <array>
#include <chrono>

namespace monopoly::timers
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        struct TimerState
        {
            std::uint16_t value = 0;
            std::uint16_t restartCount = 0;

            std::uint8_t speed =
                static_cast<std::uint8_t>(BasicClockRateHz / 10);

            std::uint8_t remainingTicks = 1;

            bool sendUIMessage = false;
        };

        std::array<TimerState, MaxTimers> states{};

        std::uint64_t globalTickCount = 0;

        Clock::time_point previousTime{};
        std::chrono::duration<double> accumulator{};

        bool initialized = false;

        constexpr std::chrono::duration<double> TickDuration{
            1.0 / static_cast<double>(BasicClockRateHz)
        };

        void sendExpiration(
            std::size_t timerIndex,
            const TimerState& timer)
        {
            // L_UIMsg.cpp original abandonne les expirations des timers
            // periodiques lorsque la file depasse sa demi-capacite.
            if (timer.restartCount != 0 &&
                uimsg::size() > uimsg::QueueCapacity / 2)
            {
                return;
            }

            uimsg::Message message{};
            message.type = uimsg::Type::TimerReachedZero;
            message.numberA = static_cast<std::int64_t>(timerIndex);
            message.numberB = static_cast<std::int64_t>(globalTickCount);

            // Une file UI pleine perdait deja les evenements periodiques
            // non essentiels dans L_UIMsg.cpp. L'echec est donc volontaire.
            (void)uimsg::send(message);
        }

        void tickOnce()
        {
            ++globalTickCount;

            for (std::size_t index = 0; index < states.size(); ++index)
            {
                TimerState& timer = states[index];

                if (timer.value == 0)
                {
                    continue;
                }

                if (--timer.remainingTicks != 0)
                {
                    continue;
                }

                timer.remainingTicks = timer.speed;

                if (--timer.value == 0)
                {
                    timer.value = timer.restartCount;

                    if (timer.sendUIMessage)
                    {
                        // Monopoly 1999: LI_TIMERS_UpdateTIMERS().
                        sendExpiration(index, timer);
                    }
                }
            }
        }
    }

    bool initialize()
    {
        states = {};

        for (TimerState& timer : states)
        {
            timer.speed =
                static_cast<std::uint8_t>(BasicClockRateHz / 10);

            timer.remainingTicks = 1;
        }

        globalTickCount = 0;
        accumulator = {};
        previousTime = Clock::now();
        initialized = true;

        return true;
    }

    void shutdown()
    {
        states = {};
        globalTickCount = 0;
        accumulator = {};
        initialized = false;
    }

    bool configure(
        std::size_t index,
        std::uint8_t speed,
        std::uint16_t restartCount,
        bool sendUIMessage,
        std::uint16_t value)
    {
        if (!initialized || index >= MaxTimers)
        {
            return false;
        }

        TimerState& timer = states[index];

        timer.speed = speed;
        timer.restartCount = restartCount;
        timer.sendUIMessage = sendUIMessage;
        timer.value = value;
        timer.remainingTicks = 1;

        return true;
    }

    void pump()
    {
        if (!initialized)
        {
            return;
        }

        const Clock::time_point now = Clock::now();

        accumulator += now - previousTime;
        previousTime = now;

        std::uint64_t elapsedTicks = 0;

        while (accumulator >= TickDuration)
        {
            accumulator -= TickDuration;
            ++elapsedTicks;
        }

        advanceTicks(elapsedTicks);
    }

    void advanceTicks(std::uint64_t numberOfTicks)
    {
        if (!initialized)
        {
            return;
        }

        for (std::uint64_t tick = 0; tick < numberOfTicks; ++tick)
        {
            tickOnce();
        }
    }

    std::uint64_t tickCount()
    {
        return globalTickCount;
    }

}
