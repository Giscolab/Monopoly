#pragma once

#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string>

namespace monopoly::video
{
    struct AviMetadata
    {
        std::uint32_t microsecondsPerFrame{};
        std::uint32_t totalFrames{};
        std::uint32_t width{};
        std::uint32_t height{};
        friend bool operator==(
            const AviMetadata&, const AviMetadata&) = default;
    };

    struct Status
    {
        std::int32_t currentFrame{-1};
        std::int32_t desiredFrame{-1};
        std::int32_t numberOfFrames{};
        bool ended{};
        friend bool operator==(const Status&, const Status&) = default;
    };

    struct JumpEvent
    {
        std::int32_t decisionFrame{-1};
        std::int32_t jumpToFrame{-1};
        bool alternativeTaken{};
        friend bool operator==(const JumpEvent&, const JumpEvent&) = default;
    };

    [[nodiscard]] std::expected<AviMetadata, std::string>
    parseAviMetadata(std::span<const std::uint8_t> bytes);

    class Runtime final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> open(
            std::span<const std::uint8_t> aviBytes,
            bool loopAtEnd = false);

        void stop() noexcept;

        [[nodiscard]] std::expected<Status, std::string> feedToFrame(
            std::int32_t desiredFrame);

        [[nodiscard]] std::expected<std::int32_t, std::string>
        frameAtElapsed(std::uint64_t elapsedMicroseconds) const;

        [[nodiscard]] std::expected<void, std::string> cutToFrame(
            std::int32_t frame);

        [[nodiscard]] std::expected<void, std::string> setAlternative(
            std::int32_t decisionFrame,
            std::int32_t jumpToFrame);

        [[nodiscard]] bool changeAlternative(
            std::int32_t decisionFrame,
            bool takeJump) noexcept;

        void forgetAlternatives() noexcept;

        [[nodiscard]] std::optional<JumpEvent> takeJumpEvent() noexcept;

        [[nodiscard]] const Status& status() const noexcept
        {
            return status_;
        }

        [[nodiscard]] const AviMetadata& metadata() const noexcept
        {
            return metadata_;
        }

        [[nodiscard]] bool open() const noexcept
        {
            return status_.numberOfFrames > 0;
        }

    private:
        struct Alternative
        {
            std::int32_t jumpToFrame{-1};
            bool takeJump{};
        };

        AviMetadata metadata_{};
        Status status_{};
        bool loopAtEnd_{};
        std::map<std::int32_t, Alternative> alternatives_;
        std::optional<JumpEvent> pendingJump_;
    };
}
