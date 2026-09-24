#pragma once

#include "ResourceRuntime.hpp"
#include "SequenceRuntime.hpp"
#include "VideoRuntime.hpp"

#include <expected>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace monopoly::video
{
    struct SequencePlaybackState
    {
        sequence::SequenceNodeId node{};
        std::filesystem::path file;
        Status status{};
        data::SequenceVideoData options{};
        std::optional<data::Sequence2DBoundingBoxAttribute> boundingBox;
        sequence::Matrix2D worldTransform{};
        bool waitingForAudioClock{};
    };

    struct SequenceJumpNotice
    {
        sequence::SequenceNodeId node{};
        JumpEvent event{};
    };
    class SequenceRuntimeBridge final
    {
    public:
        [[nodiscard]] std::expected<std::vector<SequenceJumpNotice>, std::string>
        sync(
            const sequence::SequenceRuntime& sequences,
            const data::ResourceSnapshot& resources);

        [[nodiscard]] std::expected<void, std::string> setAlternative(
            sequence::SequenceNodeId node,
            std::int32_t decisionFrame,
            std::int32_t jumpToFrame);

        [[nodiscard]] std::expected<void, std::string> chooseAlternative(
            sequence::SequenceNodeId node,
            std::int32_t decisionFrame,
            bool takeAlternative);

        [[nodiscard]] std::expected<void, std::string> forgetAlternatives(
            sequence::SequenceNodeId node);

        [[nodiscard]] std::expected<void, std::string> cutToFrame(
            sequence::SequenceNodeId node,
            std::int32_t frame);

        void reset() noexcept;

        [[nodiscard]] std::span<const SequencePlaybackState> states() const noexcept
        {
            return states_;
        }
    private:
        struct Active
        {
            Runtime runtime;
            std::filesystem::path file;
            data::SequenceVideoData options{};
            std::int32_t lastSequenceClock{
                std::numeric_limits<std::int32_t>::min()};
        };

        [[nodiscard]] std::expected<Active*, std::string> active(
            sequence::SequenceNodeId node);

        std::map<sequence::SequenceNodeId, Active> active_;
        std::vector<SequencePlaybackState> states_;
    };
}
