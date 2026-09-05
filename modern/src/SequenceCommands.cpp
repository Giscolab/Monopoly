#include "SequenceCommands.hpp"

#include <limits>
#include <type_traits>
#include <utility>

namespace monopoly::sequence
{
    SequenceCommandQueue::SequenceCommandQueue(SequenceRuntime& runtime) noexcept
        : runtime_(runtime)
    {
    }

    std::expected<void, CommandQueueError> SequenceCommandQueue::enqueueValidated(
        SequenceCommand command)
    {
        if (pending_.size() >= Capacity)
            return std::unexpected(CommandQueueError::QueueFull);
        pending_.push_back(std::move(command));
        return {};
    }

    std::expected<void, CommandQueueError> SequenceCommandQueue::enqueue(
        StartSequenceCommand command)
    {
        if (!command.program || command.program->descriptions().empty())
            return std::unexpected(CommandQueueError::InvalidProgram);
        return enqueueValidated(std::move(command));
    }

    std::expected<void, CommandQueueError> SequenceCommandQueue::enqueue(
        StopSequenceCommand command)
    {
        return enqueueValidated(command);
    }

    std::expected<void, CommandQueueError> SequenceCommandQueue::enqueue(
        SetSequenceEndingActionCommand command)
    {
        if (command.action == 0 || command.action > 3)
            return std::unexpected(CommandQueueError::InvalidEndingAction);
        return enqueueValidated(command);
    }

    std::expected<int, CommandQueueError> SequenceCommandQueue::collect()
    {
        if (nestingLevel_ == std::numeric_limits<int>::max())
            return std::unexpected(CommandQueueError::NestingOverflow);
        return ++nestingLevel_;
    }

    std::expected<int, CommandQueueError> SequenceCommandQueue::execute()
    {
        cycleError_.reset();
        if (nestingLevel_ == std::numeric_limits<int>::min())
            return std::unexpected(CommandQueueError::NestingOverflow);
        --nestingLevel_;
        if (nestingLevel_ == 0 && !pending_.empty())
        {
            // LE_SEQNCR_ProcessUserCommands maps to DoUpdateCycle(0) in the
            // single-tasking Monopoly build. Preserve the current parent time.
            drain();
            const auto updated = runtime_.update(parentClock_);
            cycleError_ = updated ? std::nullopt : std::optional<RuntimeError>(updated.error());
        }
        return nestingLevel_;
    }

    void SequenceCommandQueue::drain()
    {
        outcomes_.clear();
        while (nestingLevel_ <= 0 && !pending_.empty())
        {
            auto command = std::move(pending_.front());
            pending_.pop_front();
            std::visit([this](auto&& value)
            {
                using Command = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::is_same_v<Command, StartSequenceCommand>)
                {
                    const auto result = runtime_.start(
                        std::move(value.program), value.priority, value.options);
                    outcomes_.push_back(SequenceCommandOutcome{SequenceCommandKind::Start,
                        result ? std::optional<SequenceNodeId>(*result) : std::nullopt,
                        result ? 1U : 0U, result ? std::nullopt : std::optional<RuntimeError>(result.error())});
                }
                else if constexpr (std::is_same_v<Command, StopSequenceCommand>)
                {
                    const auto count = runtime_.stopMatching(
                        value.dataId, value.priority, value.wholeTree);
                    outcomes_.push_back(SequenceCommandOutcome{
                        SequenceCommandKind::Stop, {}, count, {}});
                }
                else
                {
                    const auto result = runtime_.setEndingActionMatching(
                        value.dataId, value.priority, value.action, value.wholeTree);
                    outcomes_.push_back(SequenceCommandOutcome{SequenceCommandKind::SetEndingAction,
                        {}, result ? *result : 0U,
                        result ? std::nullopt : std::optional<RuntimeError>(result.error())});
                }
            }, std::move(command));
        }
    }

    std::expected<void, RuntimeError> SequenceCommandQueue::updateCycle(std::int32_t parentClock)
    {
        outcomes_.clear();
        cycleError_.reset();
        parentClock_ = parentClock;
        if (nestingLevel_ <= 0) drain();
        auto updated = runtime_.update(parentClock);
        if (!updated) cycleError_ = updated.error();
        return updated;
    }

    int SequenceCommandQueue::percentageFull() const noexcept
    {
        return static_cast<int>((100U * pending_.size()) / Capacity);
    }
}
