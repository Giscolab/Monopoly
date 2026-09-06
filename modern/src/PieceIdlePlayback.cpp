#include "PieceIdlePlayback.hpp"

namespace monopoly::pieces
{
    std::expected<void, std::string> PieceIdlePlayback::begin(
        PieceIdleTransitionPlan plan)
    {
        if (plan_) return std::unexpected("piece idle playback already active");
        if (!plan.movingOut && !plan.movingIn)
            return std::unexpected("piece idle transition has no animations");
        plan_ = std::move(plan);
        movingOutSequence_ = data::EmptyDataId;
        movingInSequence_ = data::EmptyDataId;
        started_ = false;
        return {};
    }

    std::expected<void, std::string> PieceIdlePlayback::startAnimation(
        const PieceIdleAnimation& animation,
        engine::SequencePlayback& playback, data::DataId& current)
    {
        if (animation.sequence == data::EmptyDataId)
            return std::unexpected("piece idle animation has an empty DataID");
        const auto started = playback.transitionRySTxzDropStayAtEnd(
            std::nullopt, animation.sequence, TokenPriority,
            animation.startPose.yaw, 1.0F,
            animation.startPose.x, animation.startPose.z);
        if (!started) return std::unexpected(started.error());
        current = animation.sequence;
        return {};
    }

    bool PieceIdlePlayback::finished(data::DataId id,
        engine::SequencePlayback& playback) const noexcept
    {
        if (id == data::EmptyDataId) return true;
        const auto info = playback.runtime().info(id, TokenPriority, false);
        return !info || info->endTime <= info->sequenceClock;
    }

    std::expected<PieceIdlePlaybackUpdate, std::string> PieceIdlePlayback::tick(
        engine::SequencePlayback& playback)
    {
        PieceIdlePlaybackUpdate update{};
        if (!plan_) return update;

        if (!started_)
        {
            if (plan_->movingOut)
            {
                const auto out = startAnimation(
                    *plan_->movingOut, playback, movingOutSequence_);
                if (!out) return std::unexpected(out.error());
            }
            if (plan_->movingIn)
            {
                const auto in = startAnimation(
                    *plan_->movingIn, playback, movingInSequence_);
                if (!in)
                {
                    if (movingOutSequence_ != data::EmptyDataId)
                        (void)playback.stop(movingOutSequence_, TokenPriority);
                    movingOutSequence_ = data::EmptyDataId;
                    plan_.reset();
                    return std::unexpected(in.error());
                }
            }
            started_ = true;
            update.active = true;
            return update;
        }

        if (finished(movingOutSequence_, playback) &&
            movingOutSequence_ != data::EmptyDataId)
        {
            const auto stopped = playback.stop(movingOutSequence_, TokenPriority);
            if (!stopped) return std::unexpected(stopped.error());
            movingOutSequence_ = data::EmptyDataId;
        }

        if (finished(movingInSequence_, playback) &&
            movingInSequence_ != data::EmptyDataId)
        {
            const auto stopped = playback.stop(movingInSequence_, TokenPriority);
            if (!stopped) return std::unexpected(stopped.error());
            movingInSequence_ = data::EmptyDataId;
        }

        if (movingOutSequence_ == data::EmptyDataId &&
            movingInSequence_ == data::EmptyDataId)
        {
            plan_.reset();
            started_ = false;
            update.completed = true;
            return update;
        }

        update.active = true;
        return update;
    }
}
