#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace monopoly::video
{
    struct DecoderOptions
    {
        // External FFmpeg/FFprobe executables, resolved through PATH by SDL.
        std::string ffmpeg{"ffmpeg"};
        std::string ffprobe{"ffprobe"};
        std::size_t videoQueueFrames{3};
        std::size_t audioQueueChunks{16};
        std::size_t maximumFrameBytes{16U * 1024U * 1024U};
        std::uint32_t probeTimeoutMilliseconds{15000};
        bool decodeVideo{true};
        bool decodeAudio{true};
    };

    struct DecoderMetadata
    {
        std::uint32_t width{};
        std::uint32_t height{};
        std::uint32_t frameRateNumerator{};
        std::uint32_t frameRateDenominator{1};
        std::uint64_t durationMicroseconds{};
        bool hasAudio{};
        std::uint32_t audioSampleRate{};
        std::string videoCodec;
        std::string audioCodec;
    };

    struct DecodedVideoFrame
    {
        // Position on the normalized movie timeline, including the seek offset.
        // Legacy AVI/Bink frames are delivered at the reported rational cadence.
        std::uint64_t timestampMicroseconds{};
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<std::uint8_t> rgba;
    };

    struct DecodedAudioChunk
    {
        static constexpr std::uint32_t sampleRate = 48000;
        static constexpr std::uint32_t channels = 2;
        std::uint64_t timestampMicroseconds{};
        // Interleaved signed 16-bit little-endian PCM, independent of host endian.
        std::vector<std::uint8_t> pcm;
    };

    enum class DecoderPhase { Stopped, Probing, Decoding, Ended, Failed };

    struct DecoderSnapshot
    {
        DecoderPhase phase{DecoderPhase::Stopped};
        std::optional<DecoderMetadata> metadata;
        std::string error;
        std::size_t queuedVideoFrames{};
        std::size_t queuedAudioChunks{};
        std::uint64_t generation{};
        // Each stream has reached EOF; already queued output remains consumable.
        bool videoEnded{};
        bool audioEnded{};
    };

    // One worker owns all SDL process/pipe operations. Public control and queue
    // methods belong to the UI thread; they never read or drain a process pipe.
    // Decoding uses installed FFmpeg (including Indeo5/Bink decoders), not the
    // incompatible historical 32-bit codec DLLs. No shell is ever invoked.
    class Decoder final
    {
    public:
        Decoder();
        ~Decoder();
        Decoder(const Decoder&) = delete;
        Decoder& operator=(const Decoder&) = delete;
        Decoder(Decoder&&) = delete;
        Decoder& operator=(Decoder&&) = delete;

        [[nodiscard]] std::expected<void, std::string> open(
            const std::filesystem::path& file,
            const DecoderOptions& options = {});

        // Asynchronous, clears stale output immediately; zero also restarts EOF.
        [[nodiscard]] std::expected<void, std::string> seek(
            std::uint64_t timestampMicroseconds);

        // Cancels the worker, force-stops/reaps its children, then releases queues.
        // Backpressure and pending probe/decode work never need to drain first.
        void stop() noexcept;

        [[nodiscard]] DecoderSnapshot snapshot() const;
        [[nodiscard]] std::optional<DecodedVideoFrame> popVideo();
        [[nodiscard]] std::optional<DecodedAudioChunk> popAudio();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
