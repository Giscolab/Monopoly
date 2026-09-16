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
            if (state.boxActive)
            {
                result.push_back({mainId(ChatFluffButtonTag),
                    ChatFluffButtonPriority,
                    state.windowX + state.windowWidth - 40, state.windowY + 2});
            }
            if (!state.fluffOpen) return result;

            result.push_back({mainId(ChatFluffCloseTag), ChatFluffWindowPriority,
                state.fluffWindowX, state.fluffWindowY});
            result.push_back({mainId(ChatFluffShadeTag), ChatFluffWindowPriority,
                state.fluffWindowX + state.fluffWindowWidth - 22,
                state.fluffWindowY});
            for (std::size_t category = 0; category < ChatFluffCategoryTags.size(); ++category)
            {
                result.push_back({mainId(ChatFluffCategoryTags[category]),
                    ChatFluffWindowPriority,
                    state.fluffWindowX + state.fluffWindowWidth - 143 +
                        static_cast<int>(category) * 20,
                    state.fluffWindowY + 2});
            }
            if (!state.fluffShaded)
            {
                result.push_back({mainId(ChatFluffUpTag), ChatFluffWindowPriority,
                    state.fluffWindowX + state.fluffWindowWidth - 18,
                    state.fluffWindowY + 18});
                result.push_back({mainId(ChatFluffDownTag), ChatFluffWindowPriority,
                    state.fluffWindowX + state.fluffWindowWidth - 18,
                    state.fluffWindowY + state.fluffWindowHeight - 35});
            }
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

        std::vector<std::shared_ptr<const sequence::SequenceProgram>> programs;
        programs.reserve(added.size());
        for (const auto& object : added)
        {
            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), object.id);
            if (!loaded)
                return std::unexpected(
                    "UDChat fluff resource failed: " + loaded.error().detail);
            programs.push_back(std::move(*loaded));
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

        for (std::size_t index = 0; index < added.size(); ++index)
        {
            const auto& object = added[index];
            if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                    programs[index], object.priority, {},
                    sequence::moveXYTransform(object.x, object.y)}))
                return std::unexpected("validated UDChat fluff start rejected");
        }

        current_ = std::move(desired);
        return {};
    }
}
