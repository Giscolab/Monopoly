#include "VoiceChatAudioRuntime.hpp"

#include "VoiceChatRuntime.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace monopoly::voicechat
{
    namespace
    {
        constexpr std::uint16_t PcmFormatTag = 0x0001u;
        constexpr int VoiceRate = 11025;
        constexpr std::size_t CaptureReadLimit = 50000u;
        constexpr std::size_t PostSilenceStorageLimit = 11025u - 8u;
        constexpr std::size_t SilenceSubsampleStep = 16u;

        [[nodiscard]] std::string sdlFailure(const char* operation)
        {
            return std::string(operation) + ": " + SDL_GetError();
        }

        [[nodiscard]] SDL_AudioFormat pcmFormat(std::uint16_t bits) noexcept
        {
            return bits == 16u ? SDL_AUDIO_S16LE : SDL_AUDIO_U8;
        }
    }

    struct AudioRuntime::Receiver
    {
        SDL_AudioStream* stream{};
        packet::WaveFormat encodedFormat{};
        std::uint32_t maxFeedingSize{};
        std::unique_ptr<gsm610::Decoder> gsmDecoder;

        ~Receiver()
        {
            if (stream != nullptr)
                SDL_DestroyAudioStream(stream);
        }
    };

    AudioRuntime::AudioRuntime(audio::Runtime& audio) noexcept
        : audio_(audio)
    {
    }

    AudioRuntime::~AudioRuntime()
    {
        stopCapture();
        closeAllReceivers();
    }

    std::expected<void, std::string> AudioRuntime::startCapture(
        Settings settings)
    {
        stopCapture();
        const auto ready = audio_.ensureReady();
        if (!ready)
            return std::unexpected(ready.error());

        settings.silenceThreshold = std::clamp(
            settings.silenceThreshold, 0.0F, 1.0F);
        settings.volume = std::min(settings.volume, 100u);
        settings.dimensions = std::min(settings.dimensions, 3u);
        settings_ = settings;

        if (settings_.codec == Codec::Gsm610)
        {
            captureEncoder_ = std::make_unique<gsm610::Encoder>();
            if (!captureEncoder_->available())
            {
                captureEncoder_.reset();
                return std::unexpected("libgsm WAV49 encoder is unavailable");
            }
        }
        else
            captureEncoder_.reset();

        // SDL3 converts the device-native capture stream to the app-side
        // format requested here, replacing the old DirectSound conversion.
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_U8;
        spec.channels = 1;
        spec.freq = VoiceRate;
        captureStream_ = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, nullptr, nullptr);
        if (captureStream_ == nullptr)
        {
            captureEncoder_.reset();
            return std::unexpected(
                sdlFailure("SDL_OpenAudioDeviceStream(recording)"));
        }

        if (!SDL_ResumeAudioStreamDevice(captureStream_))
        {
            const auto error =
                sdlFailure("SDL_ResumeAudioStreamDevice(recording)");
            SDL_DestroyAudioStream(captureStream_);
            captureStream_ = nullptr;
            captureEncoder_.reset();
            return std::unexpected(error);
        }

        postSilenceState_ = PostSilenceState::Silence;
        firstNoiseTick_ = 0;
        lastNoiseTick_ = 0;
        postSilenceStorage_.clear();
        const auto started = sendStartPacket();
        if (!started)
        {
            SDL_DestroyAudioStream(captureStream_);
            captureStream_ = nullptr;
            captureEncoder_.reset();
            return started;
        }
        return {};
    }

    void AudioRuntime::stopCapture() noexcept
    {
        if (captureStream_ != nullptr)
        {
            std::vector<std::uint8_t> packetBytes;
            if (packet::makeStopPacket(packetBytes))
                (void)sendPacket(packetBytes, true);
            SDL_DestroyAudioStream(captureStream_);
            captureStream_ = nullptr;
        }
        captureEncoder_.reset();
        postSilenceStorage_.clear();
        postSilenceState_ = PostSilenceState::Silence;
        firstNoiseTick_ = 0;
        lastNoiseTick_ = 0;
    }

    bool AudioRuntime::captureActive() const noexcept
    {
        return captureStream_ != nullptr;
    }

    std::expected<void, std::string> AudioRuntime::sendStartPacket()
    {
        packet::WaveFormat format{};
        std::span<const std::uint8_t> extra{};
        std::array<std::uint8_t, 2> gsmExtra{};

        if (settings_.codec == Codec::Gsm610)
        {
            const auto wire = gsm610::retailWireFormat();
            format = wire.format;
            gsmExtra = wire.extra;
            extra = gsmExtra;
        }
        else
        {
            format.formatTag = PcmFormatTag;
            format.channels = 1;
            format.samplesPerSecond = VoiceRate;
            format.averageBytesPerSecond = VoiceRate;
            format.blockAlign = 1;
            format.bitsPerSample = 8;
        }

        std::vector<std::uint8_t> packetBytes;
        if (!packet::makeStartPacket(format, extra, settings_.dimensions,
                settings_.volume, packetBytes))
            return std::unexpected(
                "voice CHAT packet exceeds the retail 64K limit");
        if (!sendPacket(packetBytes, true))
            return std::unexpected(
                "unable to queue the initial voice CHAT packet");
        return {};
    }

    std::expected<void, std::string> AudioRuntime::sendData(
        std::span<const std::uint8_t> bytes, bool afterSilence)
    {
        if (bytes.empty())
            return {};
        std::vector<std::uint8_t> packetBytes;
        if (!packet::makeDataPacket(bytes, afterSilence, packetBytes))
            return std::unexpected(
                "voice data packet exceeds the retail 64K limit");

        // Retail deliberately drops non-critical voice payloads when the
        // outgoing queue is busy. That is not a capture failure.
        (void)sendPacket(packetBytes, false);
        return {};
    }

    bool AudioRuntime::containsNoise(
        std::span<const std::uint8_t> bytes) const noexcept
    {
        if (bytes.empty())
            return false;
        std::uint64_t total{};
        std::size_t count{};
        for (std::size_t offset = 0; offset < bytes.size();
             offset += SilenceSubsampleStep)
        {
            const int sample = static_cast<int>(bytes[offset]) - 128;
            total += static_cast<std::uint64_t>(std::abs(sample));
            ++count;
        }
        if (count == 0)
            return false;
        const float average = static_cast<float>(total) /
            static_cast<float>(count) / 128.0F;
        return average > settings_.silenceThreshold;
    }

    void AudioRuntime::appendHerald(std::vector<std::uint8_t>& bytes) const
    {
        const std::size_t samples = static_cast<std::size_t>(
            settings_.heraldDelayTicks) * VoiceRate / 60u;
        if (samples == 0 || bytes.size() + samples > CaptureReadLimit)
            return;

        std::vector<std::uint8_t> herald(samples, 128u);
        if (settings_.heraldSoundType == 0 && samples >= 3u)
        {
            const std::size_t third = samples / 3u;
            for (std::size_t index = 0; index < samples; ++index)
            {
                const std::size_t local =
                    index % std::max<std::size_t>(third, 1u);
                const bool middle = index >= third && index < third * 2u;
                const std::size_t period = middle ? 16u : 32u;
                herald[index] =
                    (local & (period - 1u)) < period / 2u ? 148u : 108u;
            }
        }
        else if (settings_.heraldSoundType == 1)
        {
            for (std::size_t index = 0; index < samples; ++index)
            {
                const auto reverse =
                    static_cast<std::uint32_t>(samples - index);
                const auto phase = static_cast<std::uint8_t>(
                    reverse + reverse * reverse / 8192u);
                herald[index] = (phase & 31u) < 16u ? 148u : 108u;
            }
        }
        bytes.insert(bytes.begin(), herald.begin(), herald.end());
    }

    std::expected<void, std::string> AudioRuntime::pumpCapture(
        std::uint64_t tick)
    {
        if (captureStream_ == nullptr)
            return {};

        const int available = SDL_GetAudioStreamAvailable(captureStream_);
        if (available < 0)
            return std::unexpected(
                sdlFailure("SDL_GetAudioStreamAvailable(recording)"));
        constexpr int minimum = VoiceRate / 4;
        if (available < minimum)
            return {};

        const int requested = std::min<int>(
            available, static_cast<int>(CaptureReadLimit));
        std::vector<std::uint8_t> samples(
            static_cast<std::size_t>(requested));
        const int received = SDL_GetAudioStreamData(
            captureStream_, samples.data(), requested);
        if (received < 0)
            return std::unexpected(
                sdlFailure("SDL_GetAudioStreamData(recording)"));
        if (received == 0)
            return {};
        samples.resize(static_cast<std::size_t>(received));

        const bool noisy = containsNoise(samples);
        if (noisy)
            lastNoiseTick_ = tick;
        else if (tick > lastNoiseTick_ + settings_.maxSilentTicks)
        {
            postSilenceState_ = PostSilenceState::Silence;
            postSilenceStorage_.clear();
            return {};
        }

        if (postSilenceState_ == PostSilenceState::Silence)
        {
            if (!noisy)
                return {};
            postSilenceState_ = PostSilenceState::Collecting;
            firstNoiseTick_ = lastNoiseTick_;
            appendHerald(samples);
        }

        std::vector<std::uint8_t> wireData;
        if (settings_.codec == Codec::Gsm610)
        {
            if (!captureEncoder_)
                return std::unexpected("GSM 6.10 capture encoder was lost");
            auto encoded = captureEncoder_->encodeU8(samples);
            if (!encoded)
                return std::unexpected(encoded.error());
            wireData = std::move(*encoded);
        }
        else
            wireData = std::move(samples);

        if (wireData.empty())
            return {};

        if (postSilenceState_ == PostSilenceState::Collecting)
        {
            const bool delayReached =
                tick >= firstNoiseTick_ + settings_.postSilenceDelayTicks;
            const std::size_t room = PostSilenceStorageLimit -
                std::min(postSilenceStorage_.size(),
                    PostSilenceStorageLimit);
            const bool storageFull = wireData.size() > room;
            if (!delayReached && !storageFull)
            {
                postSilenceStorage_.insert(postSilenceStorage_.end(),
                    wireData.begin(), wireData.end());
                return {};
            }

            if (!postSilenceStorage_.empty())
            {
                const auto first = sendData(postSilenceStorage_, true);
                if (!first) return first;
                postSilenceStorage_.clear();
                const auto current = sendData(wireData, false);
                if (!current) return current;
            }
            else
            {
                const auto first = sendData(wireData, true);
                if (!first) return first;
            }
            postSilenceState_ = PostSilenceState::Streaming;
            return {};
        }

        return sendData(wireData, false);
    }

    std::expected<void, std::string> AudioRuntime::openReceiver(
        const packet::Event& event, std::uint32_t sourceId)
    {
        const bool gsm = event.format.formatTag == gsm610::FormatTag;
        if (gsm && !gsm610::matchesRetailWireFormat(event))
            return std::unexpected(
                "unsupported GSM 6.10 WAVEFORMATEX parameters");
        if (!gsm && event.format.formatTag != PcmFormatTag)
            return std::unexpected("unsupported voice-chat wave format");
        if (!gsm &&
            ((event.format.bitsPerSample != 8u &&
              event.format.bitsPerSample != 16u) ||
             event.format.channels == 0u || event.format.channels > 2u ||
             event.format.samplesPerSecond == 0u ||
             event.format.samplesPerSecond > 192000u))
            return std::unexpected("invalid PCM voice-chat format");

        const auto ready = audio_.ensureReady();
        if (!ready)
            return std::unexpected(ready.error());

        SDL_AudioSpec spec{};
        if (gsm)
        {
            spec.format = SDL_AUDIO_U8;
            spec.channels = 1;
            spec.freq = static_cast<int>(event.format.samplesPerSecond);
        }
        else
        {
            spec.format = pcmFormat(event.format.bitsPerSample);
            spec.channels = static_cast<int>(event.format.channels);
            spec.freq = static_cast<int>(event.format.samplesPerSecond);
        }

        SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (stream == nullptr)
            return std::unexpected(
                sdlFailure("SDL_OpenAudioDeviceStream(voice playback)"));

        auto receiver = std::make_unique<Receiver>();
        receiver->stream = stream;
        receiver->encodedFormat = event.format;
        receiver->maxFeedingSize =
            event.format.averageBytesPerSecond / 4u;
        const auto align =
            std::max<std::uint16_t>(event.format.blockAlign, 1u);
        receiver->maxFeedingSize = std::max<std::uint32_t>(
            align, receiver->maxFeedingSize / align * align);
        if (gsm)
        {
            receiver->gsmDecoder = std::make_unique<gsm610::Decoder>();
            if (!receiver->gsmDecoder->available())
                return std::unexpected(
                    "libgsm WAV49 decoder is unavailable");
        }
        if (!SDL_ResumeAudioStreamDevice(stream))
            return std::unexpected(
                sdlFailure("SDL_ResumeAudioStreamDevice(voice playback)"));
        receivers_[sourceId] = std::move(receiver);
        return {};
    }

    bool AudioRuntime::feedReceiver(
        Receiver& receiver, std::span<const std::uint8_t> bytes,
        bool cutToCurrentData)
    {
        const auto align = std::max<std::uint16_t>(
            receiver.encodedFormat.blockAlign, 1u);
        if (bytes.size() % align != 0u)
            return false;
        if (cutToCurrentData)
        {
            if (!SDL_PauseAudioStreamDevice(receiver.stream) ||
                !SDL_ClearAudioStream(receiver.stream))
                return false;
        }

        std::size_t offset{};
        while (offset < bytes.size())
        {
            const std::size_t amount = std::min<std::size_t>(
                receiver.maxFeedingSize, bytes.size() - offset);
            const auto input = bytes.subspan(offset, amount);
            if (receiver.gsmDecoder)
            {
                auto decoded = receiver.gsmDecoder->decodeToU8(input);
                if (!decoded)
                    return false;
                if (!decoded->empty() &&
                    !SDL_PutAudioStreamData(receiver.stream,
                        decoded->data(), static_cast<int>(decoded->size())))
                    return false;
            }
            else if (!SDL_PutAudioStreamData(receiver.stream,
                input.data(), static_cast<int>(input.size())))
                return false;
            offset += amount;
        }
        return !cutToCurrentData ||
            SDL_ResumeAudioStreamDevice(receiver.stream);
    }

    bool AudioRuntime::handleEvent(
        const packet::Event& event, std::uint32_t sourceId)
    {
        if (event.kind == packet::EventKind::Start)
        {
            receivers_.erase(sourceId);
            return static_cast<bool>(openReceiver(event, sourceId));
        }

        const auto found = receivers_.find(sourceId);
        if (event.kind == packet::EventKind::Stop)
        {
            if (found != receivers_.end())
                receivers_.erase(found);
            return true;
        }
        if (found == receivers_.end())
            return true;

        auto& receiver = *found->second;
        switch (event.kind)
        {
        case packet::EventKind::Volume:
            return SDL_SetAudioStreamGain(receiver.stream,
                static_cast<float>(std::min(event.value, 100u)) / 100.0F);
        case packet::EventKind::Data:
            return feedReceiver(receiver, event.payload, false);
        case packet::EventKind::DataAfterSilence:
            return feedReceiver(receiver, event.payload, true);
        case packet::EventKind::Dimensions:
        case packet::EventKind::Position:
            return true;
        case packet::EventKind::Start:
        case packet::EventKind::Stop:
            break;
        }
        return true;
    }

    void AudioRuntime::closeAllReceivers() noexcept
    {
        receivers_.clear();
    }
}
