#include "ChatOptionPlayback.hpp"

#include <algorithm>
#include <map>
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

        [[nodiscard]] std::vector<OptionPlayback::Published> desiredObjects(
            const State& state)
        {
            std::vector<OptionPlayback::Published> result;
            if (!state.boxActive) return result;

            result.push_back({mainId(ChatOptionButtonTag),
                ChatOptionButtonPriority,
                state.windowX + state.windowWidth - 58, state.windowY + 2});
            if (state.optionsOpen)
                result.push_back({mainId(ChatOptionPanelTag),
                    ChatOptionPanelPriority,
                    state.windowX + state.windowWidth - 120, state.windowY + 16});
            return result;
        }
    }
    std::expected<void, std::string> OptionPlayback::sync(
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

        std::map<data::DataId,
            std::shared_ptr<const sequence::SequenceProgram>> programs;
        for (const auto& object : added)
        {
            if (programs.contains(object.id)) continue;
            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), object.id);
            if (!loaded)
                return std::unexpected(
                    "UDChat option resource failed: " + loaded.error().detail);
            programs.emplace(object.id, std::move(*loaded));
        }

        const std::size_t required = removed.size() + added.size();
        if (required > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit UDChat option transition");
        }

        for (const auto& object : removed)
        {
            if (!playback.commands().enqueue(
                    sequence::StopSequenceCommand{
                        object.id, object.priority, false}))
                return std::unexpected(
                    "validated UDChat option stop rejected");
        }
        for (const auto& object : added)
        {
            if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                    programs.at(object.id), object.priority, {},
                    sequence::moveXYTransform(object.x, object.y)}))
                return std::unexpected(
                    "validated UDChat option start rejected");
        }

        current_ = std::move(desired);
        return {};
    }
}
