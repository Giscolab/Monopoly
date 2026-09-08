#include "OptionsFilePlayback.hpp"

#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::optionsui
{
    namespace
    {
        [[nodiscard]] std::array<data::DataId, 6> fileSequences() noexcept
        {
            std::array<data::DataId, 6> ids{};
            ids[0] = fileSequence(FileTitleTag);
            for (std::size_t index = 0; index < FileButtonInTags.size(); ++index)
                ids[index + 1] = fileSequence(FileButtonInTags[index]);
            return ids;
        }
    }

    std::expected<void, std::string> FilePlayback::sync(
        const State& state,
        display::Screen2D desiredView,
        engine::SequencePlayback& playback)
    {
        const bool desiredVisible =
            desiredView == display::Screen2D::Options &&
            state.active && state.currentScreen == Screen::File;
        if (desiredVisible == visible_)
            return {};

        const auto ids = fileSequences();
        std::array<std::shared_ptr<const sequence::SequenceProgram>, 6> programs{};
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
            for (std::size_t index = 0; index < ids.size(); ++index)
            {
                commands.push_back(sequence::StartSequenceCommand{
                    programs[index], FileScreenPriority, {}});
                commands.push_back(sequence::SetSequenceEndingActionCommand{
                    ids[index], FileScreenPriority, FileScreenStayAtEnd, false});
            }
        }
        else
        {
            for (const auto id : ids)
            {
                commands.push_back(sequence::StopSequenceCommand{
                    id, FileScreenPriority, false});
            }
        }

        if (commands.size() > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit Options File-screen transition");
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
                    "validated Options File-screen command rejected");
        }

        visible_ = desiredVisible;
        return {};
    }
}
