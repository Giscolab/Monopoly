#pragma once

#include "PennybagsCatalog.hpp"
#include "TokenVoiceCatalog.hpp"

#include <cstdint>

namespace monopoly::engine
{
    void playWarningSound() noexcept;
    void playClickSound() noexcept;
    void playBuildSound() noexcept;
    void playUnbuildSound() noexcept;
    void playSaveFailureSound() noexcept;
    void playTokenVoice(
        std::uint8_t token,
        udsound::TokenVoiceLine line,
        udsound::TokenVoiceClipPolicy policy,
        bool watchAfterStart = false) noexcept;
    void playPennybagsVoice(
        udsound::PennybagsVoice voice,
        udsound::TokenVoiceClipPolicy policy,
        bool watchAfterStart = false) noexcept;
    [[nodiscard]] bool spokenPostLockSlotEmpty() noexcept;
    [[nodiscard]] bool isUsaBoardEdition() noexcept;
    void playJailChoiceHostComment() noexcept;
}
