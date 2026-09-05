#include "SequenceClock.hpp"

#include <limits>

namespace monopoly::sequence
{
    namespace
    {
        bool fitsClock(std::int64_t value) noexcept
        {
            return value >= std::numeric_limits<std::int32_t>::min() &&
                value <= std::numeric_limits<std::int32_t>::max();
        }
    }

    std::string_view clockErrorName(ClockError error) noexcept
    {
        switch (error)
        {
        case ClockError::UnsupportedSequenceType: return "UnsupportedSequenceType";
        case ClockError::UnsupportedScrollingWorld: return "UnsupportedScrollingWorld";
        case ClockError::InvalidCadence: return "InvalidCadence";
        case ClockError::InvalidEndingAction: return "InvalidEndingAction";
        case ClockError::NegativeEndTime: return "NegativeEndTime";
        case ClockError::ParentClockWentBackwards: return "ParentClockWentBackwards";
        case ClockError::ClockOverflow: return "ClockOverflow";
        }
        return "UnknownClockError";
    }

    std::expected<SequenceClock, ClockError> SequenceClock::start(
        const data::LegacySequenceRecord& record, ClockStartOptions options)
    {
        switch (record.chunk.id)
        {
        case 1: case 2: case 3: case 4: case 9: case 10: break;
        default: return std::unexpected(ClockError::UnsupportedSequenceType);
        }
        if (record.header.scrollingWorld)
            return std::unexpected(ClockError::UnsupportedScrollingWorld);

        SequenceClock result;
        result.timeMultiple_ = options.timeMultiple != 0 ?
            options.timeMultiple : record.header.timeMultiple;
        if (result.timeMultiple_ == 0)
            return std::unexpected(ClockError::InvalidCadence);
        result.endingAction_ = options.endingAction != 0 ?
            options.endingAction : record.header.endingAction;
        if (result.endingAction_ > 3)
            return std::unexpected(ClockError::InvalidEndingAction);
        // The decoder preserves signed disk values. Negative durations have
        // no supported runtime contract here, especially modulo for loops.
        if (record.header.endTime < 0)
            return std::unexpected(ClockError::NegativeEndTime);
        result.endTime_ = record.header.endTime == 0 ?
            InfiniteEndTime : record.header.endTime;
        result.dropFrames_ = options.dropFrames.value_or(record.header.dropFrames);
        std::int64_t initial = options.initialClockOffset;
        if (result.dropFrames_ && options.parentClockAtBirth)
            initial += static_cast<std::int64_t>(*options.parentClockAtBirth) -
                record.header.parentStartTime;
        if (!fitsClock(initial))
            return std::unexpected(ClockError::ClockOverflow);
        result.clock_ = static_cast<std::int32_t>(initial);
        return result;
    }

    std::expected<ClockUpdate, ClockError> SequenceClock::update(
        std::int32_t parentClock, bool forceReevaluation)
    {
        ClockUpdate result{ false, clock_, clock_, false, false, false, stopped_ };
        if (stopped_) return result;
        if (lastParentClock_ && parentClock < *lastParentClock_)
            return std::unexpected(ClockError::ParentClockWentBackwards);

        std::int64_t elapsed = lastParentClock_ ?
            static_cast<std::int64_t>(parentClock) - *lastParentClock_ : 0;
        if (paused_) elapsed = 0;
        // First update is immediate; skipped updates do not consume time.
        if (lastParentClock_ && elapsed < timeMultiple_ && !forceReevaluation)
            return result;
        if (!dropFrames_ && elapsed > timeMultiple_) elapsed = timeMultiple_;
        const auto next = static_cast<std::int64_t>(clock_) + elapsed;
        if (!fitsClock(next)) return std::unexpected(ClockError::ClockOverflow);

        result.updated = true;
        if (!lastParentClock_) result.previousClock.reset();
        clock_ = static_cast<std::int32_t>(next);
        lastParentClock_ = parentClock;
        if (clock_ >= endTime_ || endingAction_ == 0)
        {
            result.hitEnd = true;
            result.notifyEnd = endingAction_ != 2 ||
                !result.previousClock || *result.previousClock < endTime_;
            if (endingAction_ <= 1) stopped_ = true;
            else if (endingAction_ == 2)
            {
                clock_ = endTime_;
                // Monopoly C_ArtLib.h:528 enables this source optimization.
                timeMultiple_ = 255;
            }
            else
            {
                // Natural looping calls GoBackwardsInTime(0), NOT modulo
                // of the overshoot. Child destruction/rebirth is separate.
                clock_ = 0;
                result.restartChildren = true;
            }
        }
        result.clock = clock_;
        result.stopped = stopped_;
        return result;
    }

    std::expected<void, ClockError> SequenceClock::setPaused(
        bool paused, std::int32_t parentClock)
    {
        if (stopped_) return {};
        if (lastParentClock_ && parentClock < *lastParentClock_)
            return std::unexpected(ClockError::ParentClockWentBackwards);
        // SEQCMD_Pause only rebases on the paused -> unpaused transition.
        if (paused_ && !paused) lastParentClock_ = parentClock;
        paused_ = paused;
        return {};
    }

    ClockUpdate SequenceClock::seek(std::int32_t newTime, std::int32_t parentClock)
    {
        ClockUpdate result{ !stopped_, clock_, clock_, false, false, false, stopped_ };
        if (stopped_) return result;
        if (newTime < 0 || (newTime >= endTime_ && endingAction_ == 1))
        {
            stopped_ = true;
            result.stopped = true;
            return result;
        }
        lastParentClock_ = parentClock;
        if (endingAction_ == 3) newTime %= endTime_;
        else if (endingAction_ == 2 && newTime > endTime_) newTime = endTime_;
        clock_ = newTime;
        result.clock = clock_;
        result.restartChildren = true;
        return result;
    }

    std::expected<void, ClockError> SequenceClock::setEndingAction(std::uint8_t action)
    {
        // Public LE_SEQNCR_SetEndingAction accepts 1..3, not suicide zero.
        if (action == 0 || action > 3)
            return std::unexpected(ClockError::InvalidEndingAction);
        endingAction_ = action;
        // In particular, retain cadence 255 after a held end.
        return {};
    }
}
