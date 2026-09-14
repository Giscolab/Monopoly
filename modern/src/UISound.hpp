#pragma once

#include "TokenVoiceCatalog.hpp"

#include <cstdint>

namespace monopoly::engine
{
    void playWarningSound() noexcept;
    void playClickSound() noexcept;
    void playTokenVoice(
        std::uint8_t token,
        udsound::TokenVoiceLine line,
        udsound::TokenVoiceClipPolicy policy,
        bool watchAfterStart = false) noexcept;
}
