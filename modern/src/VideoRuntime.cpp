#include "VideoRuntime.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace monopoly::video
{
    namespace
    {
        [[nodiscard]] std::uint32_t u32le(
            std::span<const std::uint8_t> bytes,
            std::size_t offset)
        {
            return static_cast<std::uint32_t>(bytes[offset]) |
                (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
                (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
                (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
        }

        [[nodiscard]] bool fourcc(
            std::span<const std::uint8_t> bytes,
            std::size_t offset,
            const char (&id)[5])
        {
            return offset + 4 <= bytes.size() &&
                std::memcmp(bytes.data() + offset, id, 4) == 0;
        }

        [[nodiscard]] std::expected<
            std::span<const std::uint8_t>, std::string>
        findChunk(
            std::span<const std::uint8_t> bytes,
            const char (&wanted)[5])
        {
            std::size_t offset{};
            while (offset + 8 <= bytes.size())
            {
                const auto size =
                    static_cast<std::size_t>(u32le(bytes, offset + 4));
                const auto payload = offset + 8;
                if (payload + size > bytes.size())
                    return std::unexpected(
                        "AVI chunk exceeds containing RIFF data");

                if (fourcc(bytes, offset, wanted))
                    return bytes.subspan(payload, size);

                if ((fourcc(bytes, offset, "RIFF") ||
                     fourcc(bytes, offset, "LIST")) &&
                    size >= 4)
                {
                    const auto nested = bytes.subspan(
                        payload + 4, size - 4);
                    auto found = findChunk(nested, wanted);
                    if (found) return found;
                }

                offset = payload + size + (size & 1U);
            }
            return std::unexpected("AVI chunk not found");
        }

        [[nodiscard]] bool validFrame(
            const Status& status,
            std::int32_t frame) noexcept
        {
            return frame >= 0 &&
                frame < status.numberOfFrames;
        }
    }

    std::expected<AviMetadata, std::string>
    parseAviMetadata(std::span<const std::uint8_t> bytes)
    {
        if (bytes.size() < 12 ||
            !fourcc(bytes, 0, "RIFF") ||
            !fourcc(bytes, 8, "AVI "))
            return std::unexpected(
                "video is not a RIFF AVI stream");

        const auto declared =
            static_cast<std::size_t>(u32le(bytes, 4));
        if (declared + 8 > bytes.size())
            return std::unexpected(
                "AVI RIFF size exceeds supplied data");

        const auto root =
            bytes.subspan(12, declared >= 4 ? declared - 4 : 0);
        const auto avih = findChunk(root, "avih");
        if (!avih) return std::unexpected(avih.error());
        if (avih->size() < 40)
            return std::unexpected(
                "AVI main header is truncated");

        AviMetadata result{};
        result.microsecondsPerFrame = u32le(*avih, 0);
        result.totalFrames = u32le(*avih, 16);
        result.width = u32le(*avih, 32);
        result.height = u32le(*avih, 36);

        if (result.microsecondsPerFrame == 0)
            return std::unexpected(
                "AVI main header has zero frame duration");
        if (result.totalFrames == 0)
            return std::unexpected(
                "AVI main header has zero frames");
        if (result.width == 0 || result.height == 0)
            return std::unexpected(
                "AVI main header has zero dimensions");
        return result;
    }

    std::expected<void, std::string> Runtime::open(
        std::span<const std::uint8_t> aviBytes,
        bool loopAtEnd)
    {
        const auto parsed = parseAviMetadata(aviBytes);
        if (!parsed) return std::unexpected(parsed.error());

        metadata_ = *parsed;
        status_.currentFrame = -1;
        status_.desiredFrame = -1;
        if (metadata_.totalFrames >
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max()))
            return std::unexpected(
                "AVI frame count exceeds runtime range");
        status_.numberOfFrames =
            static_cast<std::int32_t>(metadata_.totalFrames);
        status_.ended = false;
        loopAtEnd_ = loopAtEnd;
        alternatives_.clear();
        return {};
    }

    void Runtime::stop() noexcept
    {
        metadata_ = {};
        status_ = {};
        status_.currentFrame = -1;
        status_.desiredFrame = -1;
        loopAtEnd_ = false;
        alternatives_.clear();
    }

    std::expected<Status, std::string> Runtime::feedToFrame(
        std::int32_t desiredFrame)
    {
        if (!open())
            return std::unexpected(
                "video runtime is not open");
        if (desiredFrame < 0)
            return std::unexpected(
                "desired video frame is negative");

        if (desiredFrame >= status_.numberOfFrames)
        {
            if (loopAtEnd_)
                desiredFrame %= status_.numberOfFrames;
            else
            {
                status_.desiredFrame = desiredFrame;
                status_.currentFrame = status_.numberOfFrames;
                status_.ended = true;
                return status_;
            }
        }

        status_.desiredFrame = desiredFrame;
        status_.ended = false;

        const auto begin =
            std::max(status_.currentFrame + 1, 0);
        for (auto it = alternatives_.lower_bound(begin);
             it != alternatives_.end() &&
             it->first <= desiredFrame; ++it)
        {
            if (!it->second.takeJump ||
                it->second.jumpToFrame < 0)
                continue;
            if (!validFrame(status_, it->second.jumpToFrame))
                return std::unexpected(
                    "video alternative jump target is out of range");
            status_.currentFrame = it->second.jumpToFrame;
            status_.desiredFrame = it->second.jumpToFrame;
            return status_;
        }

        status_.currentFrame = desiredFrame;
        return status_;
    }

    std::expected<std::int32_t, std::string>
    Runtime::frameAtElapsed(
        std::uint64_t elapsedMicroseconds) const
    {
        if (!open())
            return std::unexpected(
                "video runtime is not open");
        const auto frame =
            elapsedMicroseconds / metadata_.microsecondsPerFrame;
        if (frame >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int32_t>::max()))
            return std::unexpected(
                "elapsed video time exceeds runtime range");
        return static_cast<std::int32_t>(frame);
    }

    std::expected<void, std::string> Runtime::cutToFrame(
        std::int32_t frame)
    {
        if (!open())
            return std::unexpected(
                "video runtime is not open");
        if (!validFrame(status_, frame))
            return std::unexpected(
                "video cut target is out of range");
        status_.currentFrame = frame;
        status_.desiredFrame = frame;
        status_.ended = false;
        return {};
    }

    std::expected<void, std::string> Runtime::setAlternative(
        std::int32_t decisionFrame,
        std::int32_t jumpToFrame)
    {
        if (!open())
            return std::unexpected(
                "video runtime is not open");
        if (!validFrame(status_, decisionFrame))
            return std::unexpected(
                "video alternative decision frame is out of range");
        if (jumpToFrame >= 0 &&
            !validFrame(status_, jumpToFrame))
            return std::unexpected(
                "video alternative jump frame is out of range");

        alternatives_[decisionFrame] =
            Alternative{jumpToFrame, false};
        return {};
    }

    bool Runtime::changeAlternative(
        std::int32_t decisionFrame,
        bool takeJump) noexcept
    {
        const auto found =
            alternatives_.find(decisionFrame);
        if (found == alternatives_.end())
            return false;
        found->second.takeJump = takeJump;
        return true;
    }

    void Runtime::forgetAlternatives() noexcept
    {
        alternatives_.clear();
    }
}
