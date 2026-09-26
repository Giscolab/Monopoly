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
        bool paused{};
        std::uint32_t bytesPerFrame{};
        std::uint64_t submittedBytes{};
        std::optional<std::uint64_t> seekGeneration;

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
        voice->bytesPerFrame = static_cast<std::uint32_t>(SDL_AUDIO_FRAMESIZE(spec));

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

        voice->submittedBytes = static_cast<std::uint64_t>(bytes) * copies;
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

    std::expected<void, std::string> Runtime::playFile(
        PlaybackKey key,
        std::string_view relativePath,
        float gain,
        bool loop,
        std::uint32_t pitchHertz,
        std::int32_t panPercentage)
    {
        if (!resources_)
            return std::unexpected(
                "audio runtime has no resource snapshot");
        if (relativePath.empty())
            return std::unexpected(
                "external audio filename is empty");

        const auto ready = ensureAudio();
        if (!ready)
            return ready;

        const auto resolved = resources_->paths().resolve(relativePath);
        if (!resolved)
            return std::unexpected(dataFailure(resolved.error()));

        const auto pathBytes = resolved->u8string();
        const std::string path(
            reinterpret_cast<const char*>(pathBytes.data()),
            pathBytes.size());

        SDL_AudioSpec spec{};
        Uint8* decoded{};
        Uint32 decodedLength{};
        if (!SDL_LoadWAV(
                path.c_str(), &spec, &decoded, &decodedLength))
            return std::unexpected(
                std::string("SDL_LoadWAV: ") + SDL_GetError());

        std::vector<Uint8> pcm(decoded, decoded + decodedLength);
        SDL_free(decoded);
        if (pcm.empty())
            return std::unexpected(
                "decoded external Wave contains no PCM data");
        if (pcm.size() > static_cast<std::size_t>(INT_MAX))
            return std::unexpected(
                "decoded external Wave exceeds SDL stream limits");

        SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
            &spec,
            nullptr,
            nullptr);
        if (stream == nullptr)
            return std::unexpected(
                std::string("SDL_OpenAudioDeviceStream: ") +
                SDL_GetError());

        auto voice = std::make_unique<Voice>();
        voice->key = key;
        voice->waveDataId = data::EmptyDataId;
        voice->stream = stream;
        voice->pcm = std::move(pcm);
        voice->sourceFrequency = spec.freq > 0
            ? static_cast<std::uint32_t>(spec.freq)
            : 0U;
        voice->gain = clampedGain(gain);
        voice->pan.set(panPercentage);
        voice->loop = loop;
        voice->bytesPerFrame = static_cast<std::uint32_t>(SDL_AUDIO_FRAMESIZE(spec));

        if (!SDL_SetAudioStreamGain(stream, voice->gain))
            return std::unexpected(
                std::string("SDL_SetAudioStreamGain: ") +
                SDL_GetError());
        if (!SDL_SetAudioStreamFrequencyRatio(
                stream,
                legacyPitchFrequencyRatio(
                    pitchHertz, voice->sourceFrequency)))
            return std::unexpected(
                std::string("SDL_SetAudioStreamFrequencyRatio: ") +
                SDL_GetError());
        if (!installStereoPan(stream, voice->pan))
            return std::unexpected(
                std::string("SDL_SetAudioPostmixCallback: ") +
                SDL_GetError());

        const int bytes =
            static_cast<int>(voice->pcm.size());
        const int copies = loop ? 3 : 1;
        for (int copy = 0; copy < copies; ++copy)
            if (!SDL_PutAudioStreamData(
                    stream, voice->pcm.data(), bytes))
                return std::unexpected(
                    std::string("SDL_PutAudioStreamData: ") +
                    SDL_GetError());

        voice->submittedBytes = static_cast<std::uint64_t>(bytes) * copies;
        if (!loop && !SDL_FlushAudioStream(stream))
            return std::unexpected(
                std::string("SDL_FlushAudioStream: ") +
                SDL_GetError());
        if (!SDL_ResumeAudioStreamDevice(stream))
            return std::unexpected(
                std::string("SDL_ResumeAudioStreamDevice: ") +
                SDL_GetError());

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
        auto* voice = find(key);
        if (!voice || voice->loop == loop) return;
        // Freeze the device before sampling its cursor. Removing prequeued
        // repeats must preserve the current iteration, including after wrap.
        if (!SDL_PauseAudioStreamDevice(voice->stream)) { stop(key); return; }
        const auto queued = SDL_GetAudioStreamQueued(voice->stream);
        if (queued < 0 || voice->pcm.empty()) { stop(key); return; }
        auto offset = voice->submittedBytes -
            std::min(voice->submittedBytes, static_cast<std::uint64_t>(queued));
        if (voice->loop) offset %= voice->pcm.size();
        else offset = std::min(offset, static_cast<std::uint64_t>(voice->pcm.size()));
        voice->loop = loop;
        if (loop) offset %= voice->pcm.size();
        if (!queueVoiceFromByte(*voice, offset)) stop(key);
    }

    std::optional<std::int32_t> Runtime::positionTicks(PlaybackKey key) const noexcept
    {
        const auto* voice = find(key);
        if (!voice || !voice->stream || voice->sourceFrequency == 0 ||
            voice->bytesPerFrame == 0 || voice->pcm.empty()) return std::nullopt;
        const auto queued = SDL_GetAudioStreamQueued(voice->stream);
        if (queued < 0) return std::nullopt;
        auto consumed = voice->submittedBytes -
            std::min(voice->submittedBytes, static_cast<std::uint64_t>(queued));
        if (voice->loop) consumed %= voice->pcm.size();
        else consumed = std::min(consumed, static_cast<std::uint64_t>(voice->pcm.size()));
        const auto ticks = consumed * 60U /
            (static_cast<std::uint64_t>(voice->bytesPerFrame) * voice->sourceFrequency);
        return static_cast<std::int32_t>(std::min<std::uint64_t>(ticks,
            std::numeric_limits<std::int32_t>::max()));
    }

    std::expected<void, std::string> Runtime::seekVoice(Voice& voice, std::int32_t tick)
    {
        if (tick < 0 || voice.bytesPerFrame == 0 || voice.pcm.size() < voice.bytesPerFrame)
            return std::unexpected("invalid audio seek");
        auto offset = static_cast<std::uint64_t>(tick) * voice.sourceFrequency / 60U * voice.bytesPerFrame;
        if (voice.loop) offset %= voice.pcm.size();
        else offset = std::min(offset, static_cast<std::uint64_t>(
            voice.pcm.size() - voice.bytesPerFrame));
        return queueVoiceFromByte(voice, offset);
    }

    std::expected<void, std::string> Runtime::queueVoiceFromByte(Voice& voice, std::uint64_t offset)
    {
        if (!SDL_PauseAudioStreamDevice(voice.stream) || !SDL_ClearAudioStream(voice.stream))
            return std::unexpected(std::string("audio seek: ") + SDL_GetError());
        voice.submittedBytes = offset;
        const auto tail = voice.pcm.size() - static_cast<std::size_t>(offset);
        if (tail && !SDL_PutAudioStreamData(voice.stream,
                voice.pcm.data() + offset, static_cast<int>(tail)))
            return std::unexpected(std::string("audio seek data: ") + SDL_GetError());
        voice.submittedBytes += tail;
        if (voice.loop)
        {
            for (int copy = 0; copy < 2; ++copy)
            {
                if (!SDL_PutAudioStreamData(voice.stream, voice.pcm.data(),
                        static_cast<int>(voice.pcm.size())))
                    return std::unexpected(std::string("audio loop data: ") + SDL_GetError());
                voice.submittedBytes += voice.pcm.size();
            }
        }
        else if (!SDL_FlushAudioStream(voice.stream))
            return std::unexpected(std::string("audio seek flush: ") + SDL_GetError());
        if (!voice.paused && !SDL_ResumeAudioStreamDevice(voice.stream))
            return std::unexpected(std::string("audio seek resume: ") + SDL_GetError());
        return {};
    }

    std::expected<void, std::string> Runtime::synchronizeSequence(
        PlaybackKey key, std::int32_t position, bool paused, std::uint64_t seekGeneration)
    {
        auto* voice = find(key);
        if (!voice) return std::unexpected("sequence audio voice is absent");
        // A new voice was already queued at sample zero by play/playFile.
        // Do not clear and replay that buffer on its first synchronization.
        if (!voice->seekGeneration && position == 0 && !paused)
        {
            voice->seekGeneration = seekGeneration;
            return {};
        }
        const bool pauseChanged = voice->paused != paused;
        voice->paused = paused;
        if (voice->seekGeneration != seekGeneration)
        {
            const auto sought = seekVoice(*voice, position);
            if (!sought) return sought;
            voice->seekGeneration = seekGeneration;
        }
        else if (pauseChanged)
        {
            const bool ok = paused ? SDL_PauseAudioStreamDevice(voice->stream) :
                SDL_ResumeAudioStreamDevice(voice->stream);
            if (!ok) return std::unexpected(std::string("audio pause: ") + SDL_GetError());
        }
        return {};
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
                voice->submittedBytes += voice->pcm.size();
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
