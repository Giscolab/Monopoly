#pragma once

#include <cstddef>
#include <cstdint>

namespace monopoly::timers
{
    inline constexpr std::size_t MaxTimers = 4;
    inline constexpr std::uint32_t BasicClockRateHz = 60;

    bool initialize();
    void shutdown();

    bool configure(
        std::size_t index,
        std::uint8_t speed,
        std::uint16_t restartCount,
        bool sendUIMessage,
        std::uint16_t value
    );

    void pump();

    // Avance explicitement l'horloge ArtLib 60 Hz. Cette entree partage
    // exactement le meme chemin que pump() et permet les simulations et
    // tests deterministes sans dependre de l'horloge murale.
    void advanceTicks(std::uint64_t numberOfTicks);

    std::uint64_t tickCount();
}
