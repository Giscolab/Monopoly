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
            data::DataId id)
        {
            if (id == data::EmptyDataId)
                return false;
            const auto info = playback.runtime().info(
                id, CameraButtonPriority, false);
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
        bool iBarVisible,
        engine::SequencePlayback& playback)
    {
        auto nextState = visualState_;
        switch (visualState_)
        {
        case CameraButtonVisualState::Off:
            if (iBarVisible)
                nextState = CameraButtonVisualState::In;
            break;
        case CameraButtonVisualState::In:
            if (sequenceFinished(playback, currentSequence_))
                nextState = CameraButtonVisualState::Idle;
            break;
        case CameraButtonVisualState::Idle:
            if (!iBarVisible)
                nextState = CameraButtonVisualState::Out;
            break;
        case CameraButtonVisualState::Out:
        case CameraButtonVisualState::Pressed:
            if (sequenceFinished(playback, currentSequence_))
                nextState = CameraButtonVisualState::Off;
            break;
        }

        if (nextState == visualState_)
            return {};

        const auto desired = actionButtonSequence(buttonIndex_, nextState);
        std::shared_ptr<const sequence::SequenceProgram> program;
        if (desired != data::EmptyDataId)
        {
            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), desired);
            if (!loaded)
                return std::unexpected(loaded.error().detail);
            program = std::move(*loaded);
        }

        std::vector<sequence::SequenceCommand> commands;
        if (currentSequence_ != data::EmptyDataId)
        {
            commands.push_back(sequence::StopSequenceCommand{
                currentSequence_, CameraButtonPriority, false});
        }

        if (desired != data::EmptyDataId)
        {
            sequence::ClockStartOptions options{};
            options.dropFrames = true;
            commands.push_back(sequence::StartSequenceCommand{
                std::move(program), CameraButtonPriority, options});
            commands.push_back(sequence::makeMoveXY(
                desired, CameraButtonPriority, 0, 0));
            commands.push_back(sequence::SetSequenceEndingActionCommand{
                desired, CameraButtonPriority, endingAction(nextState), false});
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
        currentSequence_ = desired;
        return {};
    }
}
