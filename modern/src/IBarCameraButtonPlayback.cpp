#include "IBarCameraButtonPlayback.hpp"

#include <utility>
#include <variant>
#include <vector>

namespace monopoly::ibar
{
    namespace
    {
        [[nodiscard]] bool sequenceFinished(
            engine::SequencePlayback& playback,
            data::DataId id,
            std::uint16_t priority)
        {
            if (id == data::EmptyDataId)
                return false;
            const auto info = playback.runtime().info(
                id, priority, false);
            return info && info->sequenceClock >= info->endTime;
        }

        [[nodiscard]] std::uint8_t endingAction(
            CameraButtonVisualState state) noexcept
        {
            // UDIBar.cpp:1080-1089 contains a historical always-true
            // idle condition because the two final enum constants are used
            // as booleans. Preserve the resulting behavior: every Idle loops.
            return state == CameraButtonVisualState::Idle ?
                CameraButtonLoopToBeginning : CameraButtonStayAtEnd;
        }
    }

    std::expected<void, std::string> CameraButtonPlayback::sync(
        bool desired,
        engine::SequencePlayback& playback,
        bool useGreyButtons,
        bool buttonBarStable,
        bool allowIncoming)
    {
        auto nextState = visualState_;
        switch (visualState_)
        {
        case CameraButtonVisualState::Off:
            if (desired && buttonBarStable && allowIncoming)
                nextState = CameraButtonVisualState::In;
            break;
        case CameraButtonVisualState::In:
            if (sequenceFinished(playback, currentSequence_, priority_))
                nextState = CameraButtonVisualState::Idle;
            break;
        case CameraButtonVisualState::Idle:
            if (currentGrey_ != useGreyButtons ||
                (!desired && buttonBarStable))
            {
                // Source UDIBar.cpp lets a local/remote style reset leave Idle
                // even while another button is flying, but ordinary removal
                // waits for the globally stable button bar.
                nextState = CameraButtonVisualState::Out;
            }
            break;
        case CameraButtonVisualState::Out:
        case CameraButtonVisualState::Pressed:
            if (sequenceFinished(playback, currentSequence_, priority_))
                nextState = CameraButtonVisualState::Off;
            break;
        }

        if (nextState == visualState_)
            return {};

        const auto desiredSequence = actionButtonSequence(
            buttonIndex_, nextState, useGreyButtons);
        std::shared_ptr<const sequence::SequenceProgram> program;
        if (desiredSequence != data::EmptyDataId)
        {
            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), desiredSequence);
            if (!loaded)
                return std::unexpected(loaded.error().detail);
            program = std::move(*loaded);
        }

        std::vector<sequence::SequenceCommand> commands;
        if (currentSequence_ != data::EmptyDataId)
        {
            commands.push_back(sequence::StopSequenceCommand{
                currentSequence_, priority_, false});
        }

        if (desiredSequence != data::EmptyDataId)
        {
            sequence::ClockStartOptions options{};
            options.dropFrames = true;
            commands.push_back(sequence::StartSequenceCommand{
                std::move(program), priority_, options});
            commands.push_back(sequence::makeMoveXY(
                desiredSequence, priority_, 0, 0));
            commands.push_back(sequence::SetSequenceEndingActionCommand{
                desiredSequence, priority_, endingAction(nextState), false});
        }

        if (commands.size() >
            sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit IBar action-button transition");
        }

        for (auto& command : commands)
        {
            const auto queued = std::visit(
                [&](auto value)
                {
                    return playback.commands().enqueue(std::move(value));
                },
                std::move(command));
            if (!queued)
            {
                return std::unexpected(
                    "validated IBar action-button command rejected");
            }
        }

        visualState_ = nextState;
        currentSequence_ = desiredSequence;
        currentGrey_ = useGreyButtons;
        return {};
    }
}
