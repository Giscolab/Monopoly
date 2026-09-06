#include "IBarBankPlayback.hpp"

#include <utility>
#include <variant>
#include <vector>

namespace monopoly::ibar
{
    std::expected<void, std::string> BankPlayback::sync(
        bool visible,
        bool hovered,
        engine::SequencePlayback& playback)
    {
        if (!visible)
        {
            hovered = false;
        }

        const bool visibilityChanged = visible != visible_;
        const bool hoverChanged = visible && hovered != hovered_;
        if (!visibilityChanged && !hoverChanged)
        {
            return {};
        }

        std::shared_ptr<const sequence::SequenceProgram> program;
        if (visible && !visible_)
        {
            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), bankSequence());
            if (!loaded)
            {
                return std::unexpected(loaded.error().detail);
            }
            program = std::move(*loaded);
        }

        std::vector<sequence::SequenceCommand> commands;
        if (visible_ && !visible)
        {
            commands.push_back(sequence::StopSequenceCommand{
                bankSequence(), BankPriority, false});
        }
        else if (!visible_ && visible)
        {
            commands.push_back(sequence::StartSequenceCommand{
                program, BankPriority, {}});
            commands.push_back(sequence::makeMoveXY(
                bankSequence(), BankPriority, BankX, BankY));
            if (hovered)
            {
                commands.push_back(sequence::makeMoveXY(
                    bankSequence(), BankPriority, BankX, BankY + 1));
            }
        }
        else if (hoverChanged)
        {
            commands.push_back(sequence::makeMoveXY(
                bankSequence(), BankPriority,
                BankX, BankY + (hovered ? 1 : 0)));
        }

        if (commands.size() >
            sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit IBar bank transition");
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
            {
                return std::unexpected(
                    "validated IBar bank command rejected");
            }
        }

        visible_ = visible;
        hovered_ = hovered;
        return {};
    }
}
