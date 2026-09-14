#include "AudioRuntime.hpp"

#include "LegacyDataArchive.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <climits>
#include <utility>

namespace monopoly::audio
{
    struct Runtime::Voice
    {
        PlaybackKey key{};
        data::DataId waveDataId{};
        SDL_AudioStream* stream{};
        std::vector<Uint8> pcm;
        float gain{1.0F};
        bool loop{};

        ~Voice()
        {
            if (stream != nullptr)
                SDL_DestroyAudioStream(stream);
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
        bool loop)
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
        voice->gain = clampedGain(gain);
        voice->loop = loop;

        if (!SDL_SetAudioStreamGain(stream, voice->gain))
            return std::unexpected(
                std::string("SDL_SetAudioStreamGain: ") + SDL_GetError());

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
