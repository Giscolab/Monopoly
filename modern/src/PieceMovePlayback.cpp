#include "PieceMovePlayback.hpp"

#include "PiecePlacement.hpp"


namespace monopoly::pieces
{
    namespace
    {
        constexpr std::uint16_t MovePlaybackPriority = 100;
    }

    std::expected<void, std::string> PieceMovePlayback::begin(PieceMovePlan plan)
    {
        if (plan_) return std::unexpected("piece move playback already active");
        if (plan.instructions.empty())
            return std::unexpected("piece move plan has no TokenAnimStack instructions");
        if (plan.special == PieceMoveSpecial::OffBoardVictory)
        {
            if (!plan.loopBegin || !plan.loopEnd ||
                *plan.loopBegin >= *plan.loopEnd ||
                *plan.loopEnd > plan.instructions.size())
                return std::unexpected("victory TokenAnimStack loop bounds are invalid");
        }
        plan_ = std::move(plan);
        index_ = 0;
        currentSequence_ = data::EmptyDataId;
        desiredCamera_.reset();
        return {};
    }

    std::expected<void, std::string> PieceMovePlayback::startInstruction(
        const PieceMoveInstruction& instruction,
        engine::SequencePlayback& playback,
        PieceMovePlaybackUpdate& update)
    {
        if (instruction.sequence == data::EmptyDataId)
            return std::unexpected("sequence TokenAnimStack item has an empty DataID");
        const auto pose = tokenAnimationStartOrientation(
            static_cast<std::uint8_t>(instruction.startSquare), instruction.sequence);
        if (!pose) return std::unexpected("TokenAnimStack start square is invalid");
        const auto& startPose = *pose;

        const std::optional<data::DataId> previous =
            currentSequence_ == data::EmptyDataId ? std::nullopt :
                std::optional<data::DataId>(currentSequence_);
        const auto transitioned = playback.transitionRySTxzDropStayAtEnd(
            previous, instruction.sequence, MovePlaybackPriority,
            startPose.yaw, 1.0F, startPose.x, startPose.z);
        if (!transitioned) return std::unexpected(transitioned.error());
        update.stoppedSequence = previous.has_value();
        update.startedSequence = instruction.sequence;
        currentSequence_ = instruction.sequence;
        return {};
    }

    std::expected<PieceMovePlaybackUpdate, std::string> PieceMovePlayback::tick(
        bool boardVisible, engine::SequencePlayback& playback)
    {
        PieceMovePlaybackUpdate update{};
        if (!plan_) return update;
        auto& plan = *plan_;
        const auto top = plan.instructions.size() - 1U;

        bool escalate{};
        if (!boardVisible)
        {
            index_ = plan.instructions.size();
        }
        else if (index_ == 0)
        {
            escalate = true;
        }
        else
        {
            escalate = true;
            if (currentSequence_ != data::EmptyDataId)
            {
                const auto info = playback.runtime().info(
                    currentSequence_, MovePlaybackPriority, false);
                if (info && info->endTime > info->sequenceClock)
                    escalate = false;
            }
        }

        while (escalate && index_ < top &&
            plan.instructions[index_].cameraOnly)
            ++index_;

        if (escalate && index_ < plan.instructions.size())
        {
            const auto& instruction = plan.instructions[index_];
            if (!instruction.cameraOnly)
            {
                const auto started = startInstruction(instruction, playback, update);
                if (!started) return std::unexpected(started.error());
            }

            desiredCamera_ = instruction.camera;
            update.camera = instruction.camera;
            ++index_;

            if (plan.special == PieceMoveSpecial::OffBoardVictory &&
                plan.loopBegin && plan.loopEnd && index_ == *plan.loopEnd)
            {
                index_ = *plan.loopBegin;
                update.looped = true;
            }
        }

        if (index_ > top)
        {
            if (currentSequence_ != data::EmptyDataId)
            {
                const auto stopped = playback.stop(
                    currentSequence_, MovePlaybackPriority);
                if (!stopped) return std::unexpected(stopped.error());
                currentSequence_ = data::EmptyDataId;
                update.stoppedSequence = true;
            }
            plan_.reset();
            index_ = 0;
            update.completed = true;
            update.active = false;
            return update;
        }

        update.active = true;
        return update;
    }
}
