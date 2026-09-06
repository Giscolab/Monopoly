#pragma once

#include "PieceMovePlan.hpp"
#include "SequencePlayback.hpp"

#include <optional>
#include <string>

namespace monopoly::pieces
{
    struct PieceMovePlaybackUpdate
    {
        bool active{};
        bool completed{};
        bool looped{};
        bool stoppedSequence{};
        std::optional<BoardCameraView> camera;
        std::optional<data::DataId> startedSequence;
    };

    class PieceMovePlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> begin(PieceMovePlan plan);
        [[nodiscard]] std::expected<PieceMovePlaybackUpdate, std::string> tick(
            bool boardVisible, engine::SequencePlayback& playback);

        [[nodiscard]] bool active() const noexcept { return plan_.has_value(); }
        [[nodiscard]] std::size_t stackIndex() const noexcept { return index_; }
        [[nodiscard]] data::DataId currentSequence() const noexcept
        { return currentSequence_; }
        [[nodiscard]] std::optional<BoardCameraView> desiredCamera() const noexcept
        { return desiredCamera_; }

    private:
        [[nodiscard]] std::expected<void, std::string> startInstruction(
            const PieceMoveInstruction& instruction,
            engine::SequencePlayback& playback,
            PieceMovePlaybackUpdate& update);

        std::optional<PieceMovePlan> plan_;
        std::size_t index_{};
        data::DataId currentSequence_{data::EmptyDataId};
        std::optional<BoardCameraView> desiredCamera_;
    };
}
