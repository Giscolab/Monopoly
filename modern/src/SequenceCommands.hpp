#pragma once

#include "SequenceRuntime.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace monopoly::sequence
{
    enum class SequenceCommandKind { Start, Stop, Move, SetEndingAction };
    enum class CommandQueueError { QueueFull, InvalidProgram, InvalidEndingAction, NestingOverflow };

    struct StartSequenceCommand
    {
        std::shared_ptr<const SequenceProgram> program;
        std::uint16_t priority{};
        ClockStartOptions options{};
    };
    struct StopSequenceCommand
    {
        data::DataId dataId{};
        std::uint16_t priority{};
        bool wholeTree{};
    };
    struct SetSequenceEndingActionCommand
    {
        data::DataId dataId{};
        std::uint16_t priority{};
        std::uint8_t action{};
        bool wholeTree{};
    };
    struct MoveSequenceCommand
    {
        data::DataId dataId{};
        std::uint16_t priority{};
        SequenceTransform transform;
        bool wholeTree{};
    };

    using SequenceCommand = std::variant<StartSequenceCommand,
        StopSequenceCommand, MoveSequenceCommand,
        SetSequenceEndingActionCommand>;

    [[nodiscard]] MoveSequenceCommand makeMoveTheWorks(data::DataId dataId,
        std::uint16_t priority, SequenceTransform transform,
        bool wholeTree = false) noexcept;
    [[nodiscard]] MoveSequenceCommand makeMoveXY(data::DataId dataId,
        std::uint16_t priority, std::int32_t x, std::int32_t y) noexcept;
    [[nodiscard]] MoveSequenceCommand makeMoveRySTxz(data::DataId dataId,
        std::uint16_t priority, float yaw, float scale,
        float x, float z) noexcept;

    struct SequenceCommandOutcome
    {
        SequenceCommandKind kind{};
        std::optional<SequenceNodeId> startedNode;
        std::size_t matched{};
        std::optional<RuntimeError> error;
    };

    // Owner-thread FIFO matching Monopoly's non-immediate ArtLib configuration.
    // Commands remain queued while the Collect/Execute nesting level is positive.
    // Chaining is deliberately absent: active Monopoly callers always pass an
    // empty chain target, so the legacy waiting/dechained lists are not implied.
    class SequenceCommandQueue final
    {
    public:
        static constexpr std::size_t Capacity = 500;

        explicit SequenceCommandQueue(SequenceRuntime& runtime) noexcept;

        [[nodiscard]] std::expected<void, CommandQueueError> enqueue(
            StartSequenceCommand command);
        [[nodiscard]] std::expected<void, CommandQueueError> enqueue(
            StopSequenceCommand command);
        [[nodiscard]] std::expected<void, CommandQueueError> enqueue(
            MoveSequenceCommand command);
        [[nodiscard]] std::expected<void, CommandQueueError> enqueue(
            SetSequenceEndingActionCommand command);

        [[nodiscard]] std::expected<int, CommandQueueError> collect();
        // Exactly zero drains immediately by running a zero-time update cycle.
        // Like the source, unmatched extra Execute calls make the level negative.
        [[nodiscard]] std::expected<int, CommandQueueError> execute();
        [[nodiscard]] std::expected<void, RuntimeError> updateCycle(std::int32_t parentClock);

        [[nodiscard]] std::size_t pendingCount() const noexcept { return pending_.size(); }
        [[nodiscard]] int percentageFull() const noexcept;
        [[nodiscard]] int nestingLevel() const noexcept { return nestingLevel_; }
        [[nodiscard]] std::span<const SequenceCommandOutcome> outcomes() const noexcept
        { return outcomes_; }
        [[nodiscard]] const std::optional<RuntimeError>& lastCycleError() const noexcept
        { return cycleError_; }

    private:
        [[nodiscard]] std::expected<void, CommandQueueError> enqueueValidated(SequenceCommand command);
        void drain();

        SequenceRuntime& runtime_;
        std::deque<SequenceCommand> pending_;
        std::vector<SequenceCommandOutcome> outcomes_;
        std::optional<RuntimeError> cycleError_;
        int nestingLevel_{};
        std::int32_t parentClock_{};
    };
}
