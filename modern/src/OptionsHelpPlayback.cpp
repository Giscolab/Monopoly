#include "OptionsHelpPlayback.hpp"

#include <array>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::optionsui
{
    namespace
    {
        [[nodiscard]] std::array<data::DataId, 4> helpSequences() noexcept
        {
            std::array<data::DataId, 4> ids{};
            ids[0] = helpSequence(HelpTitleTag);
            for (std::size_t index = 0; index < HelpButtonInTags.size(); ++index)
                ids[index + 1] = helpSequence(HelpButtonInTags[index]);
            return ids;
        }
    }

    std::expected<void, std::string> HelpPlayback::sync(
        const State& state,
        display::Screen2D desiredView,
        engine::SequencePlayback& playback)
    {
        const bool desiredVisible =
            desiredView == display::Screen2D::Options &&
            state.active && state.currentScreen == Screen::Help;
        if (desiredVisible == visible_)
            return {};

        const auto ids = helpSequences();
        std::array<std::shared_ptr<const sequence::SequenceProgram>, 4> programs{};
        if (desiredVisible)
        {
            for (std::size_t index = 0; index < ids.size(); ++index)
            {
                auto loaded = sequence::SequenceProgram::load(
                    playback.resources(), ids[index]);
                if (!loaded)
                    return std::unexpected(loaded.error().detail);
                programs[index] = std::move(*loaded);
            }
        }

        std::vector<sequence::SequenceCommand> commands;
        if (desiredVisible)
        {
            commands.push_back(sequence::StartSequenceCommand{
                programs[0], HelpScreenPriority, {}});
            for (std::size_t index = 1; index < ids.size(); ++index)
            {
                commands.push_back(sequence::StartSequenceCommand{
                    programs[index], HelpScreenPriority, {}});
                commands.push_back(sequence::SetSequenceEndingActionCommand{
                    ids[index], HelpScreenPriority, HelpScreenStayAtEnd, false});
            }
        }
        else
        {
            for (const auto id : ids)
                commands.push_back(sequence::StopSequenceCommand{
                    id, HelpScreenPriority, false});
        }

        if (commands.size() > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
            return std::unexpected(
                "sequence command queue cannot fit Options Help-screen transition");

        for (auto& command : commands)
        {
            const auto queued = std::visit(
                [&](auto value)
                {
                    return playback.commands().enqueue(std::move(value));
                }, std::move(command));
            if (!queued)
                return std::unexpected("validated Options Help-screen command rejected");
        }

        visible_ = desiredVisible;
        return {};
    }
}
