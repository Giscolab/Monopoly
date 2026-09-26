#include "AudioRuntime.hpp"

#include "LegacyDataArchive.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <limits>
#include <utility>

namespace monopoly::audio
{
    namespace
    {
        struct StereoPanState
        {
            std::atomic<float> left{1.0F};
            std::atomic<float> right{1.0F};

            void set(std::int32_t percentage) noexcept
            {
                const auto gains = legacyPanGains(percentage);
                left.store(gains.left, std::memory_order_relaxed);
                right.store(gains.right, std::memory_order_relaxed);
            }
        };

        void SDLCALL applyStereoPan(
            void* userdata, const SDL_AudioSpec* spec,
            float* buffer, int buflen) noexcept
        {
            if (!userdata || !spec || !buffer || buflen <= 0 || spec->channels < 2)
                return;
            const auto& pan = *static_cast<const StereoPanState*>(userdata);
            const float left = pan.left.load(std::memory_order_relaxed);
            const float right = pan.right.load(std::memory_order_relaxed);
            const int samples = buflen / static_cast<int>(sizeof(float));
            const int channels = static_cast<int>(spec->channels);
            for (int sample = 0; sample + 1 < samples; sample += channels)
            {
                buffer[sample] *= left;
                buffer[sample + 1] *= right;
            }
        }

        [[nodiscard]] bool installStereoPan(
            SDL_AudioStream* stream, StereoPanState& pan) noexcept
        {
            const auto device = SDL_GetAudioStreamDevice(stream);
            return device != 0 &&
                SDL_SetAudioPostmixCallback(device, applyStereoPan, &pan);
        }

        void uninstallStereoPan(SDL_AudioStream* stream) noexcept
        {
            const auto device = SDL_GetAudioStreamDevice(stream);
            if (device != 0)
                (void)SDL_SetAudioPostmixCallback(device, nullptr, nullptr);
        }
    }

    struct Runtime::Voice
    {
        PlaybackKey key{};
        data::DataId waveDataId{};
        SDL_AudioStream* stream{};
        std::vector<Uint8> pcm;
        std::uint32_t sourceFrequency{};
        float gain{1.0F};
        StereoPanState pan;
        bool loop{};

        ~Voice()
        {
            if (stream != nullptr)
            {
                uninstallStereoPan(stream);
                SDL_DestroyAudioStream(stream);
            }
        }
    };
    namespace
    {
        [[nodiscard]] std::string dataFailure(
            const data::DataError& error)
        {
            std::string result(data::dataErrorCodeName(error.code));
            if (!error.detail.empty())
            {
                result += ": ";
                result += error.detail;
            }
            return result;
        }

        [[nodiscard]] float clampedGain(float gain) noexcept
        {
            return std::clamp(gain, 0.0F, 4.0F);
        }

        [[nodiscard]] constexpr std::uint32_t readLe32(
            std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
        {
            return static_cast<std::uint32_t>(bytes[offset]) |
                (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
                (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
                (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
        }

        [[nodiscard]] constexpr bool fourCC(
            std::span<const std::uint8_t> bytes, std::size_t offset,
            char a, char b, char c, char d) noexcept
        {
            return bytes[offset] == static_cast<std::uint8_t>(a) &&
                bytes[offset + 1U] == static_cast<std::uint8_t>(b) &&
                bytes[offset + 2U] == static_cast<std::uint8_t>(c) &&
                bytes[offset + 3U] == static_cast<std::uint8_t>(d);
        }
    }

    std::uint32_t legacyWaveDurationTicks(
        std::span<const std::uint8_t> riffWave,
        std::uint32_t ticksPerSecond) noexcept
    {
        if (riffWave.size() < 12U || ticksPerSecond == 0U ||
            !fourCC(riffWave, 0U, 'R', 'I', 'F', 'F') ||
            !fourCC(riffWave, 8U, 'W', 'A', 'V', 'E'))
            return 0U;

        const std::uint64_t riffEnd64 =
            8ULL + static_cast<std::uint64_t>(readLe32(riffWave, 4U));
        if (riffEnd64 > riffWave.size() || riffEnd64 < 12U)
            return 0U;
        const auto riffEnd = static_cast<std::size_t>(riffEnd64);

        std::uint32_t averageBytesPerSecond{};
        std::uint32_t dataBytes{};
        std::size_t offset = 12U;
        while (offset + 8U <= riffEnd)
        {
            const std::uint32_t chunkSize = readLe32(riffWave, offset + 4U);
            const std::uint64_t payloadEnd64 =
                static_cast<std::uint64_t>(offset) + 8ULL + chunkSize;
            if (payloadEnd64 > riffEnd)
                return 0U;

            if (fourCC(riffWave, offset, 'f', 'm', 't', ' '))
            {
                if (chunkSize < 12U)
                    return 0U;
                averageBytesPerSecond = readLe32(riffWave, offset + 16U);
            }
            else if (fourCC(riffWave, offset, 'd', 'a', 't', 'a'))
                dataBytes = chunkSize;

            const std::uint64_t next64 = payloadEnd64 + (chunkSize & 1U);
            if (next64 > riffEnd)
                return 0U;
            offset = static_cast<std::size_t>(next64);
        }

        if (averageBytesPerSecond == 0U || dataBytes == 0U)
            return 0U;

        const std::uint64_t duration =
            static_cast<std::uint64_t>(dataBytes) * ticksPerSecond /
            averageBytesPerSecond;
        if (duration == 0U)
            return 1U;
        return static_cast<std::uint32_t>(
            std::min<std::uint64_t>(duration,
                std::numeric_limits<std::uint32_t>::max()));
    }

    Runtime::Runtime(
        std::shared_ptr<const data::ResourceSnapshot> resources)
        : resources_(std::move(resources))
    {
    }

    Runtime::~Runtime()
    {
        stopAll();
        if (ownsAudioSubsystem_)
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    std::expected<void, std::string> Runtime::ensureReady()
    {
        return ensureAudio();
    }

    std::expected<void, std::string> Runtime::ensureAudio()
    {
        if (audioReady_)
            return {};

        if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0)
        {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
                return std::unexpected(
                    std::string("SDL_InitSubSystem(audio): ") + SDL_GetError());
            ownsAudioSubsystem_ = true;
        }

        audioReady_ = true;
        return {};
    }

    Runtime::Voice* Runtime::find(PlaybackKey key) noexcept
    {
        const auto found = std::find_if(
            voices_.begin(), voices_.end(),
            [&](const auto& voice) { return voice->key == key; });
        return found == voices_.end() ? nullptr : found->get();
    }

    const Runtime::Voice* Runtime::find(PlaybackKey key) const noexcept
    {
        const auto found = std::find_if(
            voices_.begin(), voices_.end(),
            [&](const auto& voice) { return voice->key == key; });
        return found == voices_.end() ? nullptr : found->get();
    }
    std::expected<void, std::string> Runtime::play(
        PlaybackKey key,
        data::DataId waveDataId,
        float gain,
        bool loop,
        std::uint32_t pitchHertz,
        std::int32_t panPercentage)
    {
        if (!resources_)
            return std::unexpected("audio runtime has no resource snapshot");

        const auto ready = ensureAudio();
        if (!ready)
            return ready;

        const auto metadata = resources_->banks().metadata(waveDataId);
        if (!metadata)
            return std::unexpected(dataFailure(metadata.error()));
        if (metadata->type != data::LegacyDataType::Wave)
            return std::unexpected("audio DataId is not a legacy Wave item");

        const auto loaded = resources_->banks().load(waveDataId);
        if (!loaded)
            return std::unexpected(dataFailure(loaded.error()));
        if ((*loaded)->empty())
            return std::unexpected("legacy Wave item is empty");
        if ((*loaded)->size() > static_cast<std::size_t>(INT_MAX))
            return std::unexpected("legacy Wave item exceeds SDL stream limits");
        SDL_IOStream* io = SDL_IOFromConstMem(
            (*loaded)->data(), (*loaded)->size());
        if (io == nullptr)
            return std::unexpected(
                std::string("SDL_IOFromConstMem: ") + SDL_GetError());

        SDL_AudioSpec spec{};
        Uint8* decoded{};
        Uint32 decodedLength{};
        if (!SDL_LoadWAV_IO(io, true, &spec, &decoded, &decodedLength))
            return std::unexpected(
                std::string("SDL_LoadWAV_IO: ") + SDL_GetError());

        std::vector<Uint8> pcm(decoded, decoded + decodedLength);
        SDL_free(decoded);
        if (pcm.empty())
            return std::unexpected("decoded legacy Wave contains no PCM data");
        if (pcm.size() > static_cast<std::size_t>(INT_MAX))
            return std::unexpected("decoded Wave exceeds SDL stream limits");

        SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (stream == nullptr)
            return std::unexpected(
                std::string("SDL_OpenAudioDeviceStream: ") + SDL_GetError());
        auto voice = std::make_unique<Voice>();
        voice->key = key;
        voice->waveDataId = waveDataId;
        voice->stream = stream;
        voice->pcm = std::move(pcm);
        voice->sourceFrequency = spec.freq > 0 ?
            static_cast<std::uint32_t>(spec.freq) : 0U;
        voice->gain = clampedGain(gain);
        voice->pan.set(panPercentage);
        voice->loop = loop;

        if (!SDL_SetAudioStreamGain(stream, voice->gain))
            return std::unexpected(
                std::string("SDL_SetAudioStreamGain: ") + SDL_GetError());
        if (!SDL_SetAudioStreamFrequencyRatio(
                stream,
                legacyPitchFrequencyRatio(pitchHertz, voice->sourceFrequency)))
            return std::unexpected(
                std::string("SDL_SetAudioStreamFrequencyRatio: ") + SDL_GetError());
        if (!installStereoPan(stream, voice->pan))
            return std::unexpected(
                std::string("SDL_SetAudioPostmixCallback: ") + SDL_GetError());

        const int bytes = static_cast<int>(voice->pcm.size());
        const int copies = loop ? 3 : 1;
        for (int copy = 0; copy < copies; ++copy)
            if (!SDL_PutAudioStreamData(stream, voice->pcm.data(), bytes))
                return std::unexpected(
                    std::string("SDL_PutAudioStreamData: ") + SDL_GetError());

        if (!loop && !SDL_FlushAudioStream(stream))
            return std::unexpected(
                std::string("SDL_FlushAudioStream: ") + SDL_GetError());
        if (!SDL_ResumeAudioStreamDevice(stream))
            return std::unexpected(
                std::string("SDL_ResumeAudioStreamDevice: ") + SDL_GetError());
        stop(key);
        voices_.push_back(std::move(voice));
        return {};
    }

    void Runtime::stop(PlaybackKey key) noexcept
    {
        const auto found = std::find_if(
            voices_.begin(), voices_.end(),
            [&](const auto& voice) { return voice->key == key; });
        if (found != voices_.end())
            voices_.erase(found);
    }

    void Runtime::stopAll() noexcept
    {
        voices_.clear();
    }

    void Runtime::setGain(PlaybackKey key, float gain) noexcept
    {
        if (auto* voice = find(key))
        {
            voice->gain = clampedGain(gain);
            (void)SDL_SetAudioStreamGain(voice->stream, voice->gain);
        }
    }

    void Runtime::setPitch(PlaybackKey key, std::uint32_t hertz) noexcept
    {
        if (auto* voice = find(key))
        {
            (void)SDL_SetAudioStreamFrequencyRatio(
                voice->stream,
                legacyPitchFrequencyRatio(hertz, voice->sourceFrequency));
        }
    }

    void Runtime::setPanning(
        PlaybackKey key, std::int32_t percentage) noexcept
    {
        if (auto* voice = find(key))
            voice->pan.set(percentage);
    }

    void Runtime::setLooping(PlaybackKey key, bool loop) noexcept
    {
        if (auto* voice = find(key))
            voice->loop = loop;
    }

    void Runtime::update() noexcept
    {
        for (auto& voice : voices_)
        {
            if (!voice->loop || voice->stream == nullptr || voice->pcm.empty())
                continue;

            const int queued = SDL_GetAudioStreamQueued(voice->stream);
            if (queued < 0)
                continue;

            const std::size_t target = voice->pcm.size() * 2u;
            std::size_t current = static_cast<std::size_t>(queued);
            while (current < target)
            {
                if (!SDL_PutAudioStreamData(
                        voice->stream,
                        voice->pcm.data(),
                        static_cast<int>(voice->pcm.size())))
                    break;
                current += voice->pcm.size();
            }
        }
    }

    bool Runtime::active(PlaybackKey key) const noexcept
    {
        const auto* voice = find(key);
        return voice != nullptr && voice->stream != nullptr &&
            (voice->loop || SDL_GetAudioStreamQueued(voice->stream) > 0);
    }
}
