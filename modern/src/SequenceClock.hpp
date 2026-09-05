#pragma once

#include "LegacySequence.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

namespace monopoly::sequence
{
    enum class ClockError
    {
        UnsupportedSequenceType,
        UnsupportedScrollingWorld,
        InvalidCadence,
        InvalidEndingAction,
        NegativeEndTime,
        ParentClockWentBackwards,
        ClockOverflow
    };

    [[nodiscard]] std::string_view clockErrorName(ClockError error) noexcept;

    struct ClockStartOptions
    {
        // Nonzero replacements have the source command semantics. Zero keeps
        // the corresponding disk value (L_Seqncr.cpp:3766-3784).
        std::uint8_t timeMultiple{};
        std::uint8_t endingAction{};
        std::optional<bool> dropFrames;
        std::int32_t initialClockOffset{};
        // Omit for a top-level sequence; supply the parent's current clock
        // for a newly born child. This is not its last-update timestamp.
        std::optional<std::int32_t> parentClockAtBirth;
    };

    struct ClockUpdate
    {
        bool updated{};
        // No previous clock means the initial scan begins at minus infinity.
        std::optional<std::int32_t> previousClock;
        std::int32_t clock{};
        bool hitEnd{};       // callback/watch condition, including held ends
        bool notifyEnd{};    // label event condition; held ends notify once
        bool restartChildren{};
        bool stopped{};
    };

    // Owned CPU timing state extracted from StartUpSequence and
    // UpdateSequenceClock. Consumes decoded records; no borrowed payload.
    // This is NOT a tree executor or animation renderer: the caller must
    // consume restart/stop events and manage children, commands, tweekers,
    // transforms and render slots. Sound/video hardware clocks and scrolling
    // visibility are explicitly refused, not simulated with wall time.
    class SequenceClock final
    {
    public:
        static constexpr std::int32_t InfiniteEndTime = 1'234'567'890;

        [[nodiscard]] static std::expected<SequenceClock, ClockError> start(
            const data::LegacySequenceRecord& record,
            ClockStartOptions options = {});

        // Time units are those of the parent (top-level ArtLib clock: 60 Hz).
        // No implicit wall clock. Backwards parent time requires the future
        // tree rewind operation; errors preserve all current state.
        [[nodiscard]] std::expected<ClockUpdate, ClockError> update(
            std::int32_t parentClock, bool forceReevaluation = false);

        [[nodiscard]] std::expected<void, ClockError> setPaused(
            bool paused, std::int32_t parentClock);

        // GoBackwardsInTime: explicit seek uses modulo for loops, unlike a
        // natural end. Caller destroys/recreates children and recursively
        // seeks them to newTime-parentStartTime, even with keepFrames.
        [[nodiscard]] ClockUpdate seek(std::int32_t newTime, std::int32_t parentClock);
        [[nodiscard]] std::expected<void, ClockError> setEndingAction(std::uint8_t action);

        [[nodiscard]] std::int32_t clock() const noexcept { return clock_; }
        [[nodiscard]] std::int32_t endTime() const noexcept { return endTime_; }
        [[nodiscard]] std::uint8_t timeMultiple() const noexcept { return timeMultiple_; }
        [[nodiscard]] bool paused() const noexcept { return paused_; }
        [[nodiscard]] bool stopped() const noexcept { return stopped_; }

    private:
        SequenceClock() = default;
        std::optional<std::int32_t> lastParentClock_;
        std::int32_t clock_{};
        std::int32_t endTime_{};
        std::uint8_t timeMultiple_{};
        std::uint8_t endingAction_{};
        bool dropFrames_{};
        bool paused_{};
        bool stopped_{};
    };
}
