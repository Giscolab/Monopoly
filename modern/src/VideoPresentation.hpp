#pragma once

#include "VideoDecoder.hpp"
#include "LegacyBitmap.hpp"
#include "LegacySequence.hpp"
#include "SequenceTransforms.hpp"
#include <memory>

namespace monopoly::video
{
    struct PresentationClock
    {
        std::uint64_t elapsedMicroseconds{};
        std::uint64_t consumedAudioBytes{};
        bool waitingForAudio{};
        bool audioDrained{};
    };

    // Decoder output is top-down straight RGBA. The same image is published
    // through RuntimeBitmapStore and the existing Overlay2D renderer.
    [[nodiscard]] std::expected<data::LegacyBitmapRGBA8, std::string>
    prepareVideoFrame(DecodedVideoFrame frame, const data::SequenceVideoData& options);
    [[nodiscard]] std::expected<sequence::Matrix2D, std::string>
    videoFrameTransform(std::uint32_t width, std::uint32_t height,
        const data::Sequence2DBoundingBoxAttribute& bounds,
        const sequence::Matrix2D& world);

    [[nodiscard]] data::Sequence2DBoundingBoxAttribute
    binkDoubleSizeBounds(std::uint32_t width, std::uint32_t height) noexcept;

    class Presentation final
    {
    public:
        Presentation();
        ~Presentation();
        Presentation(const Presentation&) = delete;
        Presentation& operator=(const Presentation&) = delete;
        [[nodiscard]] std::expected<void, std::string> open(
            const std::filesystem::path& file, bool enableAudio,
            const DecoderOptions& options = {});
        [[nodiscard]] DecoderSnapshot snapshot() const;
        // sequence time supplies silent movies and the tail after shorter audio.
        // Pausing does not consume PCM or advance the displayed movie clock.
        [[nodiscard]] std::expected<PresentationClock, std::string> pump(
            std::uint64_t sequenceMicroseconds, bool paused = false);
        [[nodiscard]] std::expected<std::optional<DecodedVideoFrame>, std::string>
        frameAt(std::uint64_t elapsedMicroseconds);
        [[nodiscard]] std::expected<void, std::string> seek(
            std::uint64_t timestampMicroseconds, std::uint64_t sequenceMicroseconds);
        [[nodiscard]] std::expected<void, std::string> setGain(float gain);
        [[nodiscard]] std::expected<void, std::string> setPitch(
            std::uint32_t hertz, std::uint32_t originalHertz);
        void setPanning(std::int32_t percentage) noexcept;
        [[nodiscard]] bool videoDrained() const;
        void stop() noexcept;
    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
