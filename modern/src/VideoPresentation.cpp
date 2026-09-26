#include "VideoPresentation.hpp"
#include "AudioRuntime.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

namespace monopoly::video
{
    namespace
    {
        constexpr std::uint64_t BytesPerSecond = 48000U * 2U * 2U;
        constexpr int MaximumQueuedAudio = static_cast<int>(BytesPerSecond / 2U);
        std::string sdlError(const char* operation)
        { return std::string(operation) + ": " + SDL_GetError(); }

        struct StereoPanState
        {
            std::atomic<float> left{1.0F};
            std::atomic<float> right{1.0F};
            void set(std::int32_t percentage) noexcept
            {
                const auto gains = audio::legacyPanGains(percentage);
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
    std::expected<data::LegacyBitmapRGBA8, std::string> prepareVideoFrame(
        DecodedVideoFrame frame, const data::SequenceVideoData& options)
    {
        if (!frame.width || !frame.height || frame.width > 8192 || frame.height > 8192 ||
            frame.rgba.size() != static_cast<std::uint64_t>(frame.width) * frame.height * 4U)
            return std::unexpected("decoded video frame dimensions do not match RGBA pixels");
        if (options.saturation || options.brightness || options.contrast)
            return std::unexpected("non-default legacy Indeo colour controls are unsupported");
        if (options.flipVertically)
        {
            const auto stride = static_cast<std::size_t>(frame.width) * 4U;
            for (std::size_t y = 0; y < frame.height / 2U; ++y)
                std::swap_ranges(frame.rgba.begin() + y * stride,
                    frame.rgba.begin() + (y + 1U) * stride,
                    frame.rgba.begin() + (frame.height - 1U - y) * stride);
        }
        // L_Rend2D uses drawSolid to bypass alpha; alphaLevel is already 0..255.
        for (std::size_t i = 3; i < frame.rgba.size(); i += 4)
        {
            const bool colourKey = !options.drawSolid && options.alphaLevel == 255 &&
                frame.rgba[i - 3] == 0 && frame.rgba[i - 2] == 255 && frame.rgba[i - 1] == 0;
            frame.rgba[i] = options.drawSolid ? 255 : colourKey ? 0 : options.alphaLevel;
        }
        return data::LegacyBitmapRGBA8{frame.width, frame.height, std::move(frame.rgba)};
    }
    std::expected<sequence::Matrix2D, std::string> videoFrameTransform(
        std::uint32_t width, std::uint32_t height,
        const data::Sequence2DBoundingBoxAttribute& bounds,
        const sequence::Matrix2D& world)
    {
        const auto rectangleWidth = static_cast<std::int64_t>(bounds.right) - bounds.left;
        const auto rectangleHeight = static_cast<std::int64_t>(bounds.bottom) - bounds.top;
        if (!width || !height || rectangleWidth <= 0 || rectangleHeight <= 0 ||
            !std::ranges::all_of(world.values, [](float value) { return std::isfinite(value); }))
            return std::unexpected("video rectangle or world transform is invalid");
        auto transform = sequence::identity2D();
        transform.values[0] = static_cast<float>(rectangleWidth) / width;
        transform.values[4] = static_cast<float>(rectangleHeight) / height;
        transform.values[6] = static_cast<float>(bounds.left);
        transform.values[7] = static_cast<float>(bounds.top);
        return sequence::multiply(transform, world);
    }

    data::Sequence2DBoundingBoxAttribute binkDoubleSizeBounds(
        std::uint32_t width, std::uint32_t height) noexcept
    {
        // L_Seqncr.cpp:12793: double native dimensions, center, then clamp.
        const auto doubledWidth = static_cast<std::int64_t>(width) * 2;
        const auto doubledHeight = static_cast<std::int64_t>(height) * 2;
        const auto left = std::max<std::int64_t>(0, (800 - doubledWidth) / 2);
        const auto top = std::max<std::int64_t>(0, (600 - doubledHeight) / 2);
        return {{}, static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
            static_cast<std::int32_t>(std::min<std::int64_t>(800, left + doubledWidth)),
            static_cast<std::int32_t>(std::min<std::int64_t>(600, top + doubledHeight))};
    }
    struct Presentation::Impl
    {
        Decoder decoder;
        SDL_AudioStream* stream{};
        std::optional<DecodedVideoFrame> nextFrame;
        bool audioEnabled{};
        bool resumed{};
        bool flushed{};
        bool anchored{};
        bool tailAnchored{};
        std::uint64_t sequenceOrigin{};
        std::uint64_t lastSequenceTime{};
        std::uint64_t mediaOrigin{};
        std::uint64_t submitted{};
        std::uint64_t consumed{};
        std::uint64_t audioOrigin{};
        std::uint64_t tailSequence{};
        std::uint64_t tailMedia{};
        std::uint64_t lastElapsed{};
        float gain{1.0F};
        float frequencyRatio{1.0F};
        StereoPanState pan;
        ~Impl()
        {
            if (stream)
            {
                uninstallStereoPan(stream);
                SDL_DestroyAudioStream(stream);
            }
        }
        void clearAudio()
        {
            if (stream)
            {
                uninstallStereoPan(stream);
                SDL_DestroyAudioStream(stream);
            }
            stream = nullptr;
            resumed = flushed = tailAnchored = false;
            submitted = consumed = 0;
            audioOrigin = mediaOrigin;
        }
    };
    Presentation::Presentation() : impl_(std::make_unique<Impl>()) {}
    Presentation::~Presentation() = default;
    std::expected<void, std::string> Presentation::open(
        const std::filesystem::path& file, bool enableAudio, const DecoderOptions& options)
    {
        stop();
        impl_->audioEnabled = enableAudio;
        auto selected = options;
        selected.decodeAudio = enableAudio;
        return impl_->decoder.open(file, selected);
    }
    DecoderSnapshot Presentation::snapshot() const { return impl_->decoder.snapshot(); }
    void Presentation::stop() noexcept
    {
        impl_->decoder.stop();
        impl_->clearAudio();
        impl_->nextFrame.reset();
        impl_->anchored = false;
        impl_->sequenceOrigin = impl_->lastSequenceTime = impl_->mediaOrigin = impl_->lastElapsed = 0;
    }
    std::expected<void, std::string> Presentation::seek(
        std::uint64_t timestamp, std::uint64_t sequenceTime)
    {
        const auto seeked = impl_->decoder.seek(timestamp);
        if (!seeked) return seeked;
        impl_->mediaOrigin = impl_->lastElapsed = timestamp;
        impl_->sequenceOrigin = impl_->lastSequenceTime = sequenceTime;
        impl_->anchored = true;
        impl_->clearAudio();
        impl_->nextFrame.reset();
        return {};
    }
    std::expected<void, std::string> Presentation::setGain(float gain)
    {
        if (!std::isfinite(gain))
            return std::unexpected("video audio gain is not finite");
        impl_->gain = std::clamp(gain, 0.0F, 1.0F);
        if (impl_->stream &&
            !SDL_SetAudioStreamGain(impl_->stream, impl_->gain))
            return std::unexpected(sdlError("set video audio gain"));
        return {};
    }

    std::expected<void, std::string> Presentation::setPitch(
        std::uint32_t hertz, std::uint32_t originalHertz)
    {
        impl_->frequencyRatio = audio::legacyPitchFrequencyRatio(
            hertz, originalHertz);
        if (impl_->stream &&
            !SDL_SetAudioStreamFrequencyRatio(
                impl_->stream, impl_->frequencyRatio))
            return std::unexpected(sdlError("set video audio pitch"));
        return {};
    }

    void Presentation::setPanning(std::int32_t percentage) noexcept
    {
        impl_->pan.set(percentage);
    }

    std::expected<PresentationClock, std::string> Presentation::pump(
        std::uint64_t sequenceTime, bool paused)
    {
        auto& state = *impl_;
        state.lastSequenceTime = sequenceTime;
        auto snapshot = state.decoder.snapshot();
        if (snapshot.phase == DecoderPhase::Failed)
            return std::unexpected(snapshot.error);
        if (!snapshot.metadata) return PresentationClock{};
        if (!state.anchored && !snapshot.queuedVideoFrames && !snapshot.videoEnded)
            return PresentationClock{state.mediaOrigin, 0, state.audioEnabled && snapshot.metadata->hasAudio, false};
        if (!state.anchored)
        {
            state.sequenceOrigin = sequenceTime;
            state.anchored = true;
        }
        const auto silentTime = state.mediaOrigin +
            (sequenceTime >= state.sequenceOrigin ? sequenceTime - state.sequenceOrigin : 0);
        if (!state.audioEnabled || !snapshot.metadata->hasAudio)
        {
            state.lastElapsed = paused ? state.lastElapsed : silentTime;
            return PresentationClock{state.lastElapsed, 0, false, true};
        }
        if (!state.stream)
        {
            SDL_AudioSpec spec{SDL_AUDIO_S16LE, 2, 48000};
            state.stream = SDL_OpenAudioDeviceStream(
                SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
            if (!state.stream) return std::unexpected(sdlError("open video audio stream"));
            if (!SDL_SetAudioStreamGain(state.stream, state.gain))
                return std::unexpected(sdlError("set initial video audio gain"));
            if (!SDL_SetAudioStreamFrequencyRatio(
                    state.stream, state.frequencyRatio))
                return std::unexpected(sdlError("set initial video audio pitch"));
            if (!installStereoPan(state.stream, state.pan))
                return std::unexpected(sdlError("set video audio panning callback"));
        }
        int queued = SDL_GetAudioStreamQueued(state.stream);
        if (queued < 0) return std::unexpected(sdlError("query video audio queue"));
        // Device callbacks pull PCM; total submitted minus still queued is the
        // consumed source clock. Wall time never advances through an underrun.
        state.consumed = std::max(state.consumed, state.submitted -
            std::min(state.submitted, static_cast<std::uint64_t>(queued)));
        for (int chunks = 0; chunks < 16 && queued < MaximumQueuedAudio; ++chunks)
        {
            auto chunk = state.decoder.popAudio();
            if (!chunk) break;
            if (chunk->pcm.empty() || chunk->pcm.size() % 4U ||
                chunk->pcm.size() > static_cast<std::size_t>(MaximumQueuedAudio))
                return std::unexpected("decoded video PCM block is outside stream limits");
            if (!state.submitted) state.audioOrigin = chunk->timestampMicroseconds;
            if (!SDL_PutAudioStreamData(state.stream, chunk->pcm.data(),
                    static_cast<int>(chunk->pcm.size())))
                return std::unexpected(sdlError("queue video PCM"));
            state.submitted += chunk->pcm.size();
            queued += static_cast<int>(chunk->pcm.size());
        }
        snapshot = state.decoder.snapshot();
        if (snapshot.audioEnded && !snapshot.queuedAudioChunks && !state.flushed)
        {
            if (!SDL_FlushAudioStream(state.stream))
                return std::unexpected(sdlError("flush final video PCM"));
            state.flushed = true;
        }
        const bool shouldResume = !paused && state.submitted != 0;
        if (shouldResume != state.resumed)
        {
            const bool changed = shouldResume ? SDL_ResumeAudioStreamDevice(state.stream) :
                SDL_PauseAudioStreamDevice(state.stream);
            if (!changed) return std::unexpected(sdlError("pause/resume video audio"));
            state.resumed = shouldResume;
        }
        const bool drained = snapshot.audioEnded && !snapshot.queuedAudioChunks && queued == 0;
        auto elapsed = state.audioOrigin + (state.consumed / BytesPerSecond) * 1000000U +
            (state.consumed % BytesPerSecond) * 1000000U / BytesPerSecond;
        if (drained)
        {
            if (!state.tailAnchored)
            {
                state.tailAnchored = true;
                state.tailSequence = sequenceTime;
                state.tailMedia = elapsed;
            }
            elapsed = state.tailMedia +
                (sequenceTime >= state.tailSequence ? sequenceTime - state.tailSequence : 0);
        }
        if (!paused) state.lastElapsed = std::max(state.lastElapsed, elapsed);
        return PresentationClock{state.lastElapsed, state.consumed,
            !state.submitted && !drained, drained};
    }
    std::expected<std::optional<DecodedVideoFrame>, std::string> Presentation::frameAt(
        std::uint64_t elapsed)
    {
        auto& state = *impl_;
        const auto snapshot = state.decoder.snapshot();
        if (snapshot.phase == DecoderPhase::Failed) return std::unexpected(snapshot.error);
        std::optional<DecodedVideoFrame> latest;
        // Bound UI work even if the worker replenishes its bounded queue rapidly.
        for (int frames = 0; frames < 8; ++frames)
        {
            if (!state.nextFrame) state.nextFrame = state.decoder.popVideo();
            if (!state.nextFrame || state.nextFrame->timestampMicroseconds > elapsed) break;
            latest = std::move(state.nextFrame);
            state.nextFrame.reset();
        }
        if (latest && !state.anchored)
        {
            // The decoder may publish its first frame after pump's empty-queue
            // snapshot. Anchor when that frame is actually presented, otherwise
            // the next pump would incorrectly discard all elapsed parent time.
            state.sequenceOrigin = state.lastSequenceTime;
            state.anchored = true;
        }
        return latest;
    }
    bool Presentation::videoDrained() const
    {
        const auto snapshot = impl_->decoder.snapshot();
        return snapshot.videoEnded && !snapshot.queuedVideoFrames && !impl_->nextFrame;
    }
}
