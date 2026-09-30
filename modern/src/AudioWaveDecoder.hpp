#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace monopoly::audio
{
    struct DecodedWave
    {
        SDL_AudioSpec spec{};
        std::vector<Uint8> pcm;
    };

    // SDL handles ordinary PCM/ADPCM WAVE. Retail dialog banks also contain
    // Microsoft GSM 6.10 WAV49 (format 0x0031), decoded through bundled libgsm.
    [[nodiscard]] std::expected<DecodedWave, std::string>
    decodeWave(std::span<const std::byte> bytes);
}
