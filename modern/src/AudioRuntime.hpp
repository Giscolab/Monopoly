#pragma once

#include "DataBanks.hpp"
#include "ResourceRuntime.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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

    struct StereoPanGains
    {
        float left{1.0F};
        float right{1.0F};
    };

    // Source/artlib/L_Sound.cpp::LE_SOUND_SetPanBufSnd. The retail
    // nLogVolume table encodes this percentage balance as DirectSound dB:
    // -100 is full left, 0 leaves both channels unchanged, +100 is full right.
    [[nodiscard]] constexpr StereoPanGains legacyPanGains(
        std::int32_t percentage) noexcept
    {
        const auto pan = percentage < -100 ? -100 :
            percentage > 100 ? 100 : percentage;
        if (pan <= 0)
            return {1.0F, static_cast<float>(pan + 100) / 100.0F};
        return {static_cast<float>(100 - pan) / 100.0F, 1.0F};
    }

    struct Legacy2DSoundMix
    {
        std::uint8_t volume{};
        std::int8_t panning{};
    };

    // Source/artlib/L_Rend2D.cpp::SequenceMoved. Monopoly's 2D slots use
    // the 800-pixel virtual screen and fade sounds to silence 1000 pixels
    // beyond either edge.
    [[nodiscard]] constexpr Legacy2DSoundMix legacy2DSoundMix(
        std::uint8_t baseVolume, std::int32_t centerX,
        std::int32_t left = 0, std::int32_t right = 800,
        std::int32_t quietDistance = 1000) noexcept
    {
        std::int32_t volume = baseVolume > 100U ? 100 : baseVolume;
        if (right <= left || quietDistance <= 0)
            return {static_cast<std::uint8_t>(volume), 0};

        const auto rawPan = static_cast<std::int32_t>(
            (200LL * (static_cast<std::int64_t>(centerX) - left)) /
            (right - left) - 100LL);
        std::int32_t pan = rawPan;
        std::int64_t distance{};
        if (pan < -100)
        {
            pan = -100;
            distance = static_cast<std::int64_t>(left) - centerX;
        }
        else if (pan > 100)
        {
            pan = 100;
            distance = static_cast<std::int64_t>(centerX) - right;
        }

        if (distance >= quietDistance)
            volume = 0;
        else if (distance > 0)
            volume = static_cast<std::int32_t>(
                static_cast<std::int64_t>(volume) *
                (quietDistance - distance) / quietDistance);

        return {static_cast<std::uint8_t>(volume),
            static_cast<std::int8_t>(pan)};
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
            std::uint32_t pitchHertz = 0U,
            std::int32_t panPercentage = 0);
        [[nodiscard]] std::expected<void, std::string> playFile(
            PlaybackKey key,
            std::string_view relativePath,
            float gain = 1.0F,
            bool loop = false,
            std::uint32_t pitchHertz = 0U,
            std::int32_t panPercentage = 0);
        void stop(PlaybackKey key) noexcept;
        void stopAll() noexcept;
        void setGain(PlaybackKey key, float gain) noexcept;
        void setPitch(PlaybackKey key, std::uint32_t hertz) noexcept;
        void setPanning(PlaybackKey key, std::int32_t percentage) noexcept;
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
