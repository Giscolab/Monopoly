#pragma once

#include "AudioRuntime.hpp"
#include "Gsm610Codec.hpp"
#include "VoiceChatPacket.hpp"

#include <SDL3/SDL_audio.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace monopoly::voicechat
{
    class AudioRuntime final
    {
    public:
        enum class Codec : std::uint8_t
        {
            Pcm,
            Gsm610
        };

        struct Settings
        {
            Codec codec{Codec::Gsm610};
            float silenceThreshold{0.05F};
            std::uint32_t volume{100};
            std::uint32_t dimensions{};
            std::uint32_t heraldDelayTicks{20};
            std::uint32_t postSilenceDelayTicks{120};
            std::uint32_t maxSilentTicks{360};
            int heraldSoundType{};
        };

        explicit AudioRuntime(audio::Runtime& audio) noexcept;
        ~AudioRuntime();

        AudioRuntime(const AudioRuntime&) = delete;
        AudioRuntime& operator=(const AudioRuntime&) = delete;

        [[nodiscard]] std::expected<void, std::string> startCapture(
            Settings settings = {});
        void stopCapture() noexcept;
        [[nodiscard]] std::expected<void, std::string> pumpCapture(
            std::uint64_t tick);
        [[nodiscard]] bool captureActive() const noexcept;

        [[nodiscard]] bool handleEvent(
            const packet::Event& event, std::uint32_t sourceId);
        void closeAllReceivers() noexcept;

        [[nodiscard]] static constexpr bool gsm610Supported() noexcept
        {
            return true;
        }

    private:
        enum class PostSilenceState : std::uint8_t
        {
            Silence,
            Collecting,
            Streaming
        };

        struct Receiver;

        [[nodiscard]] std::expected<void, std::string> sendStartPacket();
        [[nodiscard]] std::expected<void, std::string> sendData(
            std::span<const std::uint8_t> bytes, bool afterSilence);
        [[nodiscard]] std::expected<void, std::string> openReceiver(
            const packet::Event& event, std::uint32_t sourceId);
        [[nodiscard]] bool feedReceiver(
            Receiver& receiver, std::span<const std::uint8_t> bytes,
            bool cutToCurrentData);
        void appendHerald(std::vector<std::uint8_t>& bytes) const;
        [[nodiscard]] bool containsNoise(
            std::span<const std::uint8_t> bytes) const noexcept;

        audio::Runtime& audio_;
        SDL_AudioStream* captureStream_{};
        Settings settings_{};
        PostSilenceState postSilenceState_{PostSilenceState::Silence};
        std::uint64_t firstNoiseTick_{};
        std::uint64_t lastNoiseTick_{};
        std::vector<std::uint8_t> postSilenceStorage_;
        std::unique_ptr<gsm610::Encoder> captureEncoder_;
        std::unordered_map<std::uint32_t, std::unique_ptr<Receiver>> receivers_;
    };
}
