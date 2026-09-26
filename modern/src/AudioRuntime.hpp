#pragma once

#include "DataBanks.hpp"
#include "ResourceRuntime.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace monopoly::audio
{
    // Source/artlib/L_Sound.cpp::LE_SOUND_GetSoundDuration.
    // Invalid/malformed input returns 0 like the retail helper.
    [[nodiscard]] std::uint32_t legacyWaveDurationTicks(
        std::span<const std::uint8_t> riffWave,
        std::uint32_t ticksPerSecond = 60U) noexcept;

    // Source/artlib/L_Sound.cpp::LE_SOUND_SetPitchBufSnd.
    // Zero restores the original recording rate; DirectSound 7 clamps
    // explicit frequencies to 100..100000 Hz. SDL additionally constrains
    // its stream ratio to 0.01..100.
    [[nodiscard]] constexpr float legacyPitchFrequencyRatio(
        std::uint32_t requestedHertz,
        std::uint32_t originalHertz) noexcept
    {
        if (originalHertz == 0U || requestedHertz == 0U)
            return 1.0F;
        const auto effectiveHertz = requestedHertz < 100U ? 100U :
            requestedHertz > 100'000U ? 100'000U : requestedHertz;
        const float ratio = static_cast<float>(effectiveHertz) /
            static_cast<float>(originalHertz);
        return ratio < 0.01F ? 0.01F : ratio > 100.0F ? 100.0F : ratio;
    }

    enum class PlaybackDomain : std::uint8_t
    {
        Sequence,
        Interface,
        Music,
        Voice
    };

    struct PlaybackKey
    {
        PlaybackDomain domain{};
        std::uint64_t id{};
        auto operator<=>(const PlaybackKey&) const = default;
    };
    class Runtime final
    {
    public:
        explicit Runtime(
            std::shared_ptr<const data::ResourceSnapshot> resources);
        ~Runtime();

        Runtime(const Runtime&) = delete;
        Runtime& operator=(const Runtime&) = delete;

        [[nodiscard]] std::expected<void, std::string> ensureReady();
        [[nodiscard]] std::expected<void, std::string> play(
            PlaybackKey key,
            data::DataId waveDataId,
            float gain = 1.0F,
            bool loop = false,
            std::uint32_t pitchHertz = 0U);
        void stop(PlaybackKey key) noexcept;
        void stopAll() noexcept;
        void setGain(PlaybackKey key, float gain) noexcept;
        void setPitch(PlaybackKey key, std::uint32_t hertz) noexcept;
        void setLooping(PlaybackKey key, bool loop) noexcept;
        void update() noexcept;
        [[nodiscard]] bool active(PlaybackKey key) const noexcept;
        [[nodiscard]] data::BoardEdition boardEdition() const noexcept
        {
            return resources_ ? resources_->context().board : data::BoardEdition::Usa;
        }
        [[nodiscard]] data::LanguageId language() const noexcept
        {
            return resources_ ? resources_->context().language : data::LanguageId::EnglishUs;
        }

    private:
        struct Voice;
        [[nodiscard]] std::expected<void, std::string> ensureAudio();
        [[nodiscard]] Voice* find(PlaybackKey key) noexcept;
        [[nodiscard]] const Voice* find(PlaybackKey key) const noexcept;

        std::shared_ptr<const data::ResourceSnapshot> resources_;
        std::vector<std::unique_ptr<Voice>> voices_;
        bool ownsAudioSubsystem_{};
        bool audioReady_{};
    };
}
