#include "MousePointerPlayback.hpp"

namespace monopoly::mouse
{
    void Playback::reset() noexcept
    {
        visible_ = false;
        x_ = y_ = 0;
        authoredTransform_ = sequence::identity2D();
    }
    std::expected<void, std::string> Playback::sync(
        const State& state, engine::SequencePlayback& playback)
    {
        const bool desired = assetVisible(state);
        if (desired == visible_ && (!desired || (x_ == state.x && y_ == state.y)))
            return {};
        if (playback.commands().pendingCount() == sequence::SequenceCommandQueue::Capacity)
            return std::unexpected("sequence command queue cannot fit mouse pointer update");
        if (!desired)
        {
            const auto stopped = playback.stop(PointerDataId, PointerPriority);
            if (!stopped) return stopped;
            visible_ = false;
            return {};
        }
        if (!visible_)
        {
            const auto program = sequence::SequenceProgram::load(playback.resources(), PointerDataId);
            if (!program) return std::unexpected("mouse pointer resource failed: " + program.error().detail);
            const auto& root = (*program)->descriptions().front();
            const auto initial = sequence::initialSequenceTransform(root.record, root.attributes, 2);
            const auto* authored = std::get_if<sequence::Matrix2D>(&initial.local);
            const auto base = authored ? *authored : sequence::identity2D();
            // L_Seqncr.cpp:12224 uses positive hotspot offsets; the active
            // DISPLAY/UDChat caller passes (0,0). Keep authored asset transforms.
            const auto positioned = sequence::multiply(base, sequence::translate2D(state.x, state.y));
            sequence::ClockStartOptions options;
            options.endingAction = 3; // AddMouseSubSequenceTheWorks: LoopToBeginning.
            const auto started = playback.commands().enqueue(sequence::StartSequenceCommand{
                *program, PointerPriority, options, positioned});
            if (!started) return std::unexpected("mouse pointer start rejected");
            authoredTransform_ = base;
            visible_ = true;
        }
        else
        {
            const auto moved = playback.move(PointerDataId, PointerPriority,
                sequence::multiply(authoredTransform_, sequence::translate2D(state.x, state.y)));
            if (!moved) return moved;
        }
        x_ = state.x;
        y_ = state.y;
        return {};
    }
}
