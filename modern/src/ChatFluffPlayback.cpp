#include "ChatFluffPlayback.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace monopoly::chat
{
    namespace
    {
        [[nodiscard]] constexpr data::DataId mainId(data::DataTag tag) noexcept
        {
            return data::packDataId(data::LegacyGroupId::Main, tag);
        }

        [[nodiscard]] std::vector<FluffPlayback::Published> desiredObjects(
            const State& state)
        {
            std::vector<FluffPlayback::Published> result;
            if (!state.boxActive) return result;

            result.push_back({mainId(ChatFluffButtonTag),
                ChatFluffButtonPriority,
                state.windowX + state.windowWidth - 40, state.windowY + 2});
            return result;
        }
    }

    std::expected<void, std::string> FluffPlayback::sync(
        const State& state,
        engine::SequencePlayback& playback)
    {
        auto desired = desiredObjects(state);
        if (desired == current_) return {};

        std::vector<Published> removed;
        std::vector<Published> added;
        for (const auto& object : current_)
            if (std::find(desired.begin(), desired.end(), object) == desired.end())
                removed.push_back(object);
        for (const auto& object : desired)
            if (std::find(current_.begin(), current_.end(), object) == current_.end())
                added.push_back(object);

        std::shared_ptr<const sequence::SequenceProgram> program;
        if (!added.empty())
        {
            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), added.front().id);
            if (!loaded)
                return std::unexpected(
                    "UDChat fluff resource failed: " + loaded.error().detail);
            program = std::move(*loaded);
        }

        const std::size_t required = removed.size() + added.size();
        if (required > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
            return std::unexpected(
                "sequence command queue cannot fit UDChat fluff transition");

        for (const auto& object : removed)
            if (!playback.commands().enqueue(sequence::StopSequenceCommand{
                    object.id, object.priority, false}))
                return std::unexpected("validated UDChat fluff stop rejected");

        for (const auto& object : added)
            if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                    program, object.priority, {},
                    sequence::moveXYTransform(object.x, object.y)}))
                return std::unexpected("validated UDChat fluff start rejected");

        current_ = std::move(desired);
        return {};
    }
}
