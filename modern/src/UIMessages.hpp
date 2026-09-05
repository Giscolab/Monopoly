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

    // Retire les expirations deja en file lors de l'arret d'un timer de jeu.
    // Les autres timers et entrees conservent strictement leur ordre FIFO.
    std::size_t discardTimerEvents(std::size_t timerIndex);

    std::size_t size();
}

