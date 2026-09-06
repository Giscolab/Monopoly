#include "DiceDisplay.hpp"

#include "SequenceTransforms.hpp"

namespace monopoly::dice
{
    namespace
    {
        constexpr std::uint8_t StayAtEnd = 2;
        constexpr std::uint8_t LoopToBeginning = 3;

        [[nodiscard]] data::DataId threeDSequence(
            data::DataTag base,
            const std::array<std::uint8_t, 2>& values) noexcept
        {
            if (!validValues(values)) return data::EmptyDataId;
            const auto offset = static_cast<std::uint32_t>(values[0] - 1U) * 12U +
                static_cast<std::uint32_t>(values[1] - 1U) * 2U;
            return data::packDataId(data::LegacyGroupId::ThreeD,
                static_cast<data::DataTag>(base + offset));
        }

        [[nodiscard]] sequence::SequenceTransform diceTransform() noexcept
        {
            return sequence::SequenceTransform(
                sequence::moveRySTxzTransform(0.0F, 1.0F, 0.0F, 0.0F));
        }
    }

    bool validValues(const std::array<std::uint8_t, 2>& values) noexcept
    {
        return values[0] >= 1 && values[0] <= 6 &&
            values[1] >= 1 && values[1] <= 6;
    }

    data::DataId roll3DSequence(
        const std::array<std::uint8_t, 2>& values) noexcept
    {
        return threeDSequence(Roll3DBaseTag, values);
    }

    data::DataId idle3DSequence(
        const std::array<std::uint8_t, 2>& values) noexcept
    {
        return threeDSequence(Idle3DBaseTag, values);
    }

    TwoDPlan plan2D(const std::array<std::uint8_t, 2>& values,
        bool rollAnimationDesired, bool iBarVisible) noexcept
    {
        TwoDPlan plan{};
        if (rollAnimationDesired && iBarVisible)
        {
            plan.bobbing = true;
            const auto bob = data::packDataId(
                data::LegacyGroupId::Main, Bobbing2DTag);
            plan.dice[0] = TwoDItem{bob, IBarGeneralPriority,
                -35, 0, false, true};
            plan.dice[1] = TwoDItem{bob,
                static_cast<std::uint16_t>(IBarGeneralPriority + 1U),
                -11, 0, true, true};
            return plan;
        }

        if (!iBarVisible) return plan;
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            const auto face = values[index];
            if (face < 1 || face > 6) continue;
            const auto id = data::packDataId(data::LegacyGroupId::Main,
                static_cast<data::DataTag>(Fixed2DBaseTag + face - 1U));
            plan.dice[index] = TwoDItem{id,
                static_cast<std::uint16_t>(IBarGeneralPriority + index),
                static_cast<std::int32_t>(index * 24) - 35, 0,
                false, false};
        }
        return plan;
    }

    std::expected<void, std::string> TwoDPlayback::sync(
        const std::array<std::uint8_t, 2>& values, bool rollAnimationDesired,
        bool iBarVisible, bool& diceRollNotification, engine::SequencePlayback& playback)
    {
        const auto plan = plan2D(values, rollAnimationDesired, iBarVisible);
        std::vector<sequence::SequenceCommand> commands;
        auto fixed = currentDiceID_;
        auto bob = currentBobDice_;
        bool notification = diceRollNotification;
        const auto start = [&](const TwoDItem& item) -> std::expected<void, std::string> {
            auto program = sequence::SequenceProgram::load(playback.resources(), item.sequence);
            if (!program) return std::unexpected(program.error().detail);
            sequence::ClockStartOptions options{};
            options.dropFrames = item.dropFrames;
            commands.push_back(sequence::StartSequenceCommand{*program,item.priority,options});
            commands.push_back(sequence::makeMoveXY(item.sequence,item.priority,item.x,item.y));
            if (item.loop) commands.push_back(sequence::SetSequenceEndingActionCommand{
                item.sequence,item.priority,LoopToBeginning,false});
            return {};
        };
        for (std::size_t index=0;index<2;++index)
        {
            const auto desired = !plan.bobbing && plan.dice[index] ?
                plan.dice[index]->sequence : data::EmptyDataId;
            const auto priority=static_cast<std::uint16_t>(IBarGeneralPriority+index);
            if (fixed[index]!=desired || notification)
            {
                if (fixed[index]!=data::EmptyDataId)
                    commands.push_back(sequence::StopSequenceCommand{fixed[index],priority,false});
                fixed[index]=desired;
                if (desired!=data::EmptyDataId)
                {
                    const auto ready=start(*plan.dice[index]);
                    if (!ready) return ready;
                }
            }
            notification=false; // Source consumes the notification inside the loop.
        }
        const auto desiredBob=plan.bobbing ? plan.dice[0]->sequence : data::EmptyDataId;
        if (bob!=desiredBob)
        {
            if (bob!=data::EmptyDataId)
            {
                commands.push_back(sequence::StopSequenceCommand{bob,IBarGeneralPriority,false});
                commands.push_back(sequence::StopSequenceCommand{bob,IBarGeneralPriority+1,false});
            }
            bob=desiredBob;
            if (bob!=data::EmptyDataId)
                for (const auto& item:plan.dice)
                {
                    const auto ready=start(*item);
                    if (!ready) return ready;
                }
        }
        // Load all programs before touching FIFO/state. Capacity is checked
        // for the complete Stop/Start/Move/Loop operation, not per die.
        if (commands.size()>sequence::SequenceCommandQueue::Capacity-playback.commands().pendingCount())
            return std::unexpected("sequence command queue cannot fit dice 2D transition");
        for (auto& command:commands)
        {
            const auto queued=std::visit([&](auto value) {
                return playback.commands().enqueue(std::move(value));
            },std::move(command));
            if (!queued) return std::unexpected("validated dice 2D command rejected");
        }
        currentDiceID_=fixed;
        currentBobDice_=bob;
        diceRollNotification=notification;
        return {};
    }

    std::expected<void, std::string> Playback::begin(RollRequest request)
    {
        if (request_)
            return std::unexpected("dice roll playback already active");
        request_ = request;
        prepared_ = false;
        rollStarted_ = false;
        cameraReleased_ = false;
        return {};
    }

    void Playback::reset() noexcept
    {
        request_.reset();
        current3D_ = data::EmptyDataId;
        cameraSetTime_ = 0;
        prepared_ = false;
        rollStarted_ = false;
        cameraReleased_ = false;
    }

    std::expected<bool, std::string> Playback::syncIdle(
        bool boardVisible,
        const rules::GameState& state,
        engine::SequencePlayback& playback)
    {
        const auto desired = boardVisible ? idle3DSequence(state.dice) :
            data::EmptyDataId;
        if (desired == current3D_) return false;

        if (desired == data::EmptyDataId)
        {
            if (current3D_ != data::EmptyDataId)
            {
                const auto stopped = playback.stop(current3D_, Generic3DPriority);
                if (!stopped) return std::unexpected(stopped.error());
                current3D_ = data::EmptyDataId;
                return true;
            }
            return false;
        }

        const std::optional<data::DataId> previous =
            current3D_ == data::EmptyDataId ? std::nullopt :
                std::optional<data::DataId>(current3D_);
        const auto transitioned = playback.transitionMovedDrop(
            previous, desired, Generic3DPriority,
            diceTransform(), StayAtEnd);
        if (!transitioned) return std::unexpected(transitioned.error());
        current3D_ = desired;
        return true;
    }

    std::expected<PlaybackUpdate, std::string> Playback::tick(
        std::uint64_t tick,
        bool boardVisible,
        bool iBarVisible,
        const rules::GameState& state,
        engine::SequencePlayback& playback)
    {
        PlaybackUpdate update{};
        if (!request_)
        {
            const auto idle = syncIdle(boardVisible, state, playback);
            if (!idle) return std::unexpected(idle.error());
            update.idleChanged = *idle;
            return update;
        }

        update.activeRoll = true;
        const auto elapsed = tick >= request_->lockTick ?
            tick - request_->lockTick : 0U;
        const auto rollId = roll3DSequence(request_->values);

        if (!prepared_)
        {
            if (cameraSetTime_ != request_->lockTick)
            {
                cameraSetTime_ = request_->lockTick;
                update.cameraTakeover = true;
            }
            if (current3D_ != rollId &&
                current3D_ != data::EmptyDataId)
            {
                const auto stopped = playback.stop(
                    current3D_, Generic3DPriority);
                if (!stopped) return std::unexpected(stopped.error());
                current3D_ = data::EmptyDataId;
            }
            prepared_ = true;
        }

        if (elapsed > 35U && rollId != data::EmptyDataId && !rollStarted_)
        {
            if (current3D_ != rollId)
            {
                const auto started = playback.transitionMovedDrop(
                    std::nullopt, rollId, Generic3DPriority,
                    diceTransform(), StayAtEnd);
                if (!started) return std::unexpected(started.error());
                current3D_ = rollId;
            }
            rollStarted_ = true;
            update.startedRoll = true;
        }

        if (elapsed > 90U && !cameraReleased_)
        {
            cameraReleased_ = true;
            update.cameraRelease = true;
            update.announceRoll = true;
        }

        if (elapsed > 120U || !iBarVisible)
        {
            request_.reset();
            prepared_ = false;
            rollStarted_ = false;
            cameraReleased_ = false;
            update.activeRoll = false;
            update.queueRelease = true;

            const auto idle = syncIdle(boardVisible, state, playback);
            if (!idle) return std::unexpected(idle.error());
            update.idleChanged = *idle;
        }
        return update;
    }
}
