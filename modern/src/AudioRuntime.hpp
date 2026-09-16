#pragma once

#include "DataBanks.hpp"
#include "ResourceRuntime.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace monopoly::audio
{
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
            bool loop = false);
        void stop(PlaybackKey key) noexcept;
        void stopAll() noexcept;
        void setGain(PlaybackKey key, float gain) noexcept;
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
