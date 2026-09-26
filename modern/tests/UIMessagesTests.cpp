#include "UIMessages.hpp"

#include <chrono>
#include <cstddef>
#include <future>
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

    void testQueueUtilityContract()
    {
        using namespace monopoly::uimsg;

        expect(initialize(), "queue initializes for utility contract");
        expect(percentageFull() == 0, "empty queue reports zero percent full");

        for (std::size_t index = 0; index < 33; ++index)
            expect(send({Type::KeyboardPressed, static_cast<std::int64_t>(index)}),
                "utility fixture enqueues important event");
        expect(percentageFull() == 33,
            "percentageFull preserves the retail integer queue percentage");

        flushEvents();
        expect(size() == 0 && percentageFull() == 0,
            "flushEvents removes every queued event and resets percentage");

        Message payload{};
        payload.type = Type::VideoJump;
        payload.numberA = 12;
        payload.numberB = 34;
        payload.numberC = 1;
        payload.numberD = 56;
        payload.numberE = 78;
        payload.text = "owned payload";
        expect(send(payload), "generic sequence/video-style payload enqueues");

        Message received{};
        expect(receive(received) &&
            received.type == Type::VideoJump &&
            received.numberA == 12 && received.numberB == 34 &&
            received.numberC == 1 && received.numberD == 56 &&
            received.numberE == 78 && received.text == "owned payload",
            "queue preserves all generic message fields and owned text");
        shutdown();
    }

    void testWaitContract()
    {
        using namespace monopoly::uimsg;
        using namespace std::chrono_literals;

        expect(initialize(), "queue initializes for wait contract");
        auto waiter = std::async(std::launch::async, []
        {
            Message message{};
            const bool received = wait(message);
            return std::pair{received, message};
        });

        expect(waiter.wait_for(20ms) == std::future_status::timeout,
            "wait blocks while an initialized queue is empty");
        expect(send({Type::MouseMiddleDown, 321, 222}),
            "sending an event wakes a waiting receiver");
        expect(waiter.wait_for(1s) == std::future_status::ready,
            "wait returns after an event arrives");
        const auto [received, message] = waiter.get();
        expect(received && message.type == Type::MouseMiddleDown &&
            message.numberA == 321 && message.numberB == 222,
            "wait returns the exact FIFO message that woke it");

        auto shutdownWaiter = std::async(std::launch::async, []
        {
            Message message{};
            return wait(message);
        });
        expect(shutdownWaiter.wait_for(20ms) == std::future_status::timeout,
            "second wait blocks before shutdown");
        shutdown();
        expect(shutdownWaiter.wait_for(1s) == std::future_status::ready &&
            !shutdownWaiter.get(),
            "shutdown wakes waiters without fabricating an event");
    }
}

int main()
{
    std::cout
        << "Monopoly UI message queue tests\n"
        << "===============================\n";

    testLifecycleAndFifo();
    testPressurePolicy();
    testQueueUtilityContract();
    testWaitContract();

    if (failures != 0)
    {
        std::cerr << failures << " UI message test(s) failed.\n";
        return 1;
    }

    std::cout << "All UI message queue tests passed.\n";
    return 0;
}
