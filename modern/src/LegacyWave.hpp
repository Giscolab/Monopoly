#pragma once

#include <cstdint>
#include <span>

namespace monopoly::audio
{
    // Source/artlib/L_Sound.cpp::LE_SOUND_GetSoundDuration.
    // Invalid/malformed input returns 0 like the retail helper.
    [[nodiscard]] std::uint32_t legacyWaveDurationTicks(
        std::span<const std::uint8_t> riffWave,
        std::uint32_t ticksPerSecond = 60U) noexcept;
}
