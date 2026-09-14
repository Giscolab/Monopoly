#pragma once

#include "Display.hpp"
#include "PennybagsCatalog.hpp"
#include "RuleTypes.hpp"
#include "TokenVoiceCatalog.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace monopoly::ui::playersetupsound
{
    struct VoiceRequest
    {
        udsound::PennybagsVoice voice{udsound::PennybagsVoice::WelcomeGame};
        udsound::TokenVoiceClipPolicy policy{
            udsound::TokenVoiceClipPolicy::ClipOldSoundIfPlaying};
    };

    struct State
    {
        bool welcomeMessagePlayed{};
        std::optional<udsound::PennybagsVoice> playing;
        std::optional<udsound::PennybagsVoice> desired;
        udsound::TokenVoiceClipPolicy policy{
            udsound::TokenVoiceClipPolicy::ClipOldSoundIfPlaying};
    };
    struct Update
    {
        std::array<VoiceRequest, 2> requests{};
        std::size_t count{};
    };

    // DISPLAY_UDPSEL_Initialize() resets desired/playing sound state but the
    // retail static udpsel_WelcomeMessagePlayed survives for process lifetime.
    void resetPlayback(State& state) noexcept;

    [[nodiscard]] Update startPhase(
        State& state,
        display::PlayerSetupPhase phase,
        bool aiPlayer,
        std::uint8_t numberOfPlayers) noexcept;
}
