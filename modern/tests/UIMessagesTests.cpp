#include "UIMessages.hpp"

#include <cstddef>
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

    void testLifecycleAndFifo()
    {
        using namespace monopoly::uimsg;

        shutdown();

        expect(
            !send({ Type::Quit }),
            "send before initialize is rejected"
        );

        expect(initialize(), "queue initializes");
        expect(send({ Type::KeyboardPressed, 10 }), "first event enqueues");
        expect(send({ Type::KeyboardReleased, 11 }), "second event enqueues");

        Message first{};
        Message second{};

        expect(
            receive(first) &&
            first.type == Type::KeyboardPressed &&
            first.numberA == 10,
            "first event is received first"
        );

        expect(
            receive(second) &&
            second.type == Type::KeyboardReleased &&
            second.numberA == 11,
            "second event preserves FIFO order"
        );

        shutdown();

        expect(size() == 0, "shutdown clears the queue");

        Message unused{};
        expect(!receive(unused), "receive after shutdown is rejected");
    }

    void testPressurePolicy()
    {
        using namespace monopoly::uimsg;

        initialize();

        for (std::size_t index = 0;
             index <= QueueCapacity / 2;
             ++index)
        {
            expect(
                send({ Type::KeyboardPressed }),
                "important event accepted below total capacity"
            );
        }

        const std::size_t heldEvents = size();

        expect(
            !send({ Type::MouseMoved, 100, 200 }),
            "mouse motion is dropped above half capacity"
        );

        expect(
            size() == heldEvents,
            "dropped mouse motion does not alter queue order"
        );

        while (size() < QueueCapacity)
        {
            expect(
                send({ Type::KeyboardReleased }),
                "important event fills remaining queue capacity"
            );
        }

        expect(
            !send({ Type::Quit }),
            "all event types are rejected at hard capacity"
        );

        expect(
            size() == QueueCapacity,
            "queue never grows beyond its fixed capacity"
        );

        shutdown();
    }
}

int main()
{
    std::cout
        << "Monopoly UI message queue tests\n"
        << "===============================\n";

    testLifecycleAndFifo();
    testPressurePolicy();

    if (failures != 0)
    {
        std::cerr << failures << " UI message test(s) failed.\n";
        return 1;
    }

    std::cout << "All UI message queue tests passed.\n";
    return 0;
}
