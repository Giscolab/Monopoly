#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace monopoly::uimsg
{
    inline constexpr std::size_t QueueCapacity = 100;

    enum class Type : std::uint8_t
    {
        TimerReachedZero,
        KeyboardPressed,
        KeyboardReleased,
        TextInput,
        MouseMoved,
        MouseLeftDown,
        MouseLeftUp,
        Quit
    };

    struct Message
    {
        Type type{};

        std::int64_t numberA = 0;
        std::int64_t numberB = 0;
        std::int64_t numberC = 0;
        std::int64_t numberD = 0;
        std::int64_t numberE = 0;

        std::string text;
    };

    bool initialize();
    void shutdown();

    bool send(const Message& message);
    bool receive(Message& message);

    std::size_t size();
}

