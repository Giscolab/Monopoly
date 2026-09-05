#include "SequencePlayback.hpp"

namespace monopoly::engine
{
    std::expected<void, std::string> SequencePlayback::start(
        data::DataId id, std::uint16_t priority)
    {
        auto program = sequence::SequenceProgram::load(meshes_.resources(), id);
        if (!program) return std::unexpected(program.error().detail);
        const auto queued = commands_.enqueue(sequence::StartSequenceCommand{*program, priority});
        if (!queued)
            return std::unexpected("sequence command queue capacity exceeded");
        return {};
    }

    std::expected<void, std::string> SequencePlayback::startMoved(
        data::DataId id, std::uint16_t priority,
        sequence::SequenceTransform transform)
    {
        auto program = sequence::SequenceProgram::load(meshes_.resources(), id);
        if (!program) return std::unexpected(program.error().detail);
        // Start + MoveTheWorks is one historical collect-command unit in
        // UDBoard.cpp. Preflight both FIFO slots so a full queue cannot leave
        // a started-but-unscaled board behind.
        if (commands_.pendingCount() > sequence::SequenceCommandQueue::Capacity - 2)
            return std::unexpected("sequence command queue capacity exceeded");
        auto queued = commands_.enqueue(sequence::StartSequenceCommand{*program, priority});
        if (!queued) return std::unexpected("sequence command queue capacity exceeded");
        queued = commands_.enqueue(sequence::makeMoveTheWorks(
            id, priority, std::move(transform)));
        if (!queued) return std::unexpected("sequence command queue capacity exceeded");
        return {};
    }

    std::expected<void, std::string> SequencePlayback::stop(
        data::DataId id, std::uint16_t priority)
    {
        const auto queued = commands_.enqueue(
            sequence::StopSequenceCommand{id, priority});
        if (!queued) return std::unexpected("sequence command queue capacity exceeded");
        return {};
    }

    std::expected<void, std::string> SequencePlayback::update(std::int32_t tick)
    {
        const auto updated = commands_.updateCycle(tick);
        if (!updated) { world_.clear(); return std::unexpected(updated.error().detail); }
        for (const auto& outcome : commands_.outcomes())
            if (outcome.error)
            { world_.clear(); return std::unexpected(outcome.error->detail); }
        auto items = sequence::collectSequenceMeshRenderData(runtime_, meshes_);
        if (!items)
        { world_.clear(); return std::unexpected(items.error().cause.detail); }
        const auto published = world_.sync(*items);
        if (!published)
        { world_.clear(); return std::unexpected("duplicate sequence render node"); }
        return {};
    }
}
