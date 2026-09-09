#include "OptionsOptionPlayback.hpp"

#include <array>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::optionsui
{
    std::expected<void, std::string> OptionPlayback::sync(
        const State& state,
        display::Screen2D desiredView,
        engine::SequencePlayback& playback)
    {
        const bool desiredVisible =
            desiredView == display::Screen2D::Options &&
            state.active && state.currentScreen == Screen::Option;
        if (desiredVisible == visible_)
            return {};

        std::array<std::shared_ptr<const sequence::SequenceProgram>, 4> programs{};
        if (desiredVisible)
        {
            for (std::size_t index = 0; index < OptionScreenTags.size(); ++index)
            {
                auto loaded = sequence::SequenceProgram::load(
                    playback.resources(), optionSequence(OptionScreenTags[index]));
                if (!loaded)
                    return std::unexpected(loaded.error().detail);
                programs[index] = std::move(*loaded);
            }
        }

        std::vector<sequence::SequenceCommand> commands;
        if (desiredVisible)
        {
            for (std::size_t index = 0; index < OptionScreenTags.size(); ++index)
            {
                const auto id = optionSequence(OptionScreenTags[index]);
                commands.push_back(sequence::StartSequenceCommand{
                    programs[index], OptionScreenPriority, {}});
                if (OptionScreenTags[index] == OptionOkayInTag)
                {
                    commands.push_back(sequence::makeMoveXY(
                        id, OptionScreenPriority, OptionOkayX, OptionOkayY));
                    commands.push_back(sequence::SetSequenceEndingActionCommand{
                        id, OptionScreenPriority, OptionScreenStayAtEnd, false});
                }
            }
        }
        else
        {
            for (const auto tag : OptionScreenTags)
            {
                commands.push_back(sequence::StopSequenceCommand{
                    optionSequence(tag), OptionScreenPriority, false});
            }
        }

        if (commands.size() > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit Options Option-screen transition");
        }

        for (auto& command : commands)
        {
            const auto queued = std::visit(
                [&](auto value)
                {
                    return playback.commands().enqueue(std::move(value));
                },
                std::move(command));
            if (!queued)
                return std::unexpected(
                    "validated Options Option-screen command rejected");
        }

        visible_ = desiredVisible;
        return {};
    }
}
