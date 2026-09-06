#pragma once

#include "PieceIdleTransition.hpp"
#include "PieceRuntime.hpp"
#include "SequencePlayback.hpp"

#include <expected>
#include <optional>
#include <string>

namespace monopoly::pieces
{
    struct PieceIdlePlaybackUpdate
    {
        bool active{};
        bool completed{};
    };

    class PieceIdlePlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> begin(
            PieceIdleTransitionPlan plan);
        [[nodiscard]] std::expected<PieceIdlePlaybackUpdate, std::string> tick(
            engine::SequencePlayback& playback);

        [[nodiscard]] bool active() const noexcept { return plan_.has_value(); }
        [[nodiscard]] data::DataId movingOutSequence() const noexcept
        { return movingOutSequence_; }
        [[nodiscard]] data::DataId movingInSequence() const noexcept
        { return movingInSequence_; }
        [[nodiscard]] std::optional<rules::PlayerNumber> movingOutPlayer() const noexcept
        { return plan_ && plan_->movingOut ? std::optional<rules::PlayerNumber>(plan_->movingOut->player) : std::nullopt; }
        [[nodiscard]] std::optional<rules::PlayerNumber> movingInPlayer() const noexcept
        { return plan_ && plan_->movingIn ? std::optional<rules::PlayerNumber>(plan_->movingIn->player) : std::nullopt; }

    private:
        [[nodiscard]] std::expected<void, std::string> startAnimation(
            const PieceIdleAnimation& animation,
            engine::SequencePlayback& playback, data::DataId& current);
        [[nodiscard]] bool finished(data::DataId id,
            engine::SequencePlayback& playback) const noexcept;

        std::optional<PieceIdleTransitionPlan> plan_;
        data::DataId movingOutSequence_{data::EmptyDataId};
        data::DataId movingInSequence_{data::EmptyDataId};
        bool started_{};
    };
}
