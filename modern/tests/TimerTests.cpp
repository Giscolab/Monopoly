#include "Timers.hpp"
#include "UIMessages.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace
{
    int failures = 0;

    void expect(bool condition, std::string_view description)
    {
        if (condition)
        {
            std::cout << "[PASS] " << description << '\n';
            return;
        }

        ++failures;
        std::cerr << "[FAIL] " << description << '\n';
    }

    bool receiveTimer(
        std::int64_t expectedIndex,
        std::int64_t expectedTick)
    {
        monopoly::uimsg::Message message{};

        return monopoly::uimsg::receive(message) &&
               message.type == monopoly::uimsg::Type::TimerReachedZero &&
               message.numberA == expectedIndex &&
               message.numberB == expectedTick;
    }

    void resetSystems()
    {
        monopoly::timers::shutdown();
        monopoly::uimsg::shutdown();

        expect(
            monopoly::uimsg::initialize(),
            "UIMessages initializes"
        );

        expect(
            monopoly::timers::initialize(),
            "Timers initialize"
        );
    }

    void testPeriodicTimerContract()
    {
        resetSystems();

        expect(
            monopoly::timers::configure(0, 2, 1, true, 1),
            "MAIN_GAME_TIMER configuration is accepted"
        );

        monopoly::timers::advanceTicks(1);

        expect(
            receiveTimer(0, 1),
            "first expiration carries timer index and global tick"
        );

        monopoly::timers::advanceTicks(1);

        expect(
            monopoly::uimsg::size() == 0,
            "speed two waits for the second 60 Hz interval"
        );

        monopoly::timers::advanceTicks(1);

        expect(
            receiveTimer(0, 3),
            "periodic timer restarts with the original cadence"
        );
    }

    void testOneShotAndSilentTimers()
    {
        resetSystems();

        expect(
            monopoly::timers::configure(2, 1, 0, true, 1),
            "one-shot timer configuration is accepted"
        );

        monopoly::timers::advanceTicks(5);

        expect(
            receiveTimer(2, 1),
            "one-shot timer emits its first expiration"
        );

        expect(
            monopoly::uimsg::size() == 0,
            "one-shot timer remains stopped after expiration"
        );

        expect(
            monopoly::timers::configure(3, 1, 1, false, 1),
            "silent timer configuration is accepted"
        );

        monopoly::timers::advanceTicks(3);

        expect(
            monopoly::uimsg::size() == 0,
            "timer with UI notification disabled emits no message"
        );
    }

    void testInvalidIndex()
    {
        resetSystems();

        expect(
            !monopoly::timers::configure(
                monopoly::timers::MaxTimers,
                1,
                1,
                true,
                1
            ),
            "out-of-range timer index is rejected"
        );
    }
}

int main()
{
    std::cout
        << "Monopoly timer contract tests\n"
        << "==============================\n";

    testPeriodicTimerContract();
    testOneShotAndSilentTimers();
    testInvalidIndex();

    monopoly::timers::shutdown();
    monopoly::uimsg::shutdown();

    if (failures != 0)
    {
        std::cerr << failures << " timer test(s) failed.\n";
        return 1;
    }

    std::cout << "All timer contract tests passed.\n";
    return 0;
}
