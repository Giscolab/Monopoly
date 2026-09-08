#include "TradeContractDialogPlayback.hpp"

#include <array>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::tradeui
{
    std::expected<void, std::string> ContractDialogPlayback::sync(
        const State& state,
        display::Screen2D desiredView,
        engine::SequencePlayback& playback)
    {
        const bool desired =
            desiredView == display::Screen2D::Trade &&
            state.contractDialogVisible;
        if (desired == visible_)
            return {};

        constexpr std::size_t ArrowCount = 2;
        std::array<std::shared_ptr<const sequence::SequenceProgram>,
            ArrowCount> programs{};
        if (desired)
        {
            for (std::size_t index = 0; index < ArrowCount; ++index)
            {
                auto loaded = sequence::SequenceProgram::load(
                    playback.resources(), tradeContractArrowSequence(index));
                if (!loaded)
                    return std::unexpected(loaded.error().detail);
                programs[index] = std::move(*loaded);
            }
        }

        std::vector<sequence::SequenceCommand> commands;
        for (std::size_t index = 0; index < ArrowCount; ++index)
        {
            const auto id = tradeContractArrowSequence(index);
            if (!desired)
            {
                commands.push_back(sequence::StopSequenceCommand{
                    id, TradeContractArrowPriority, false});
                continue;
            }

            commands.push_back(sequence::StartSequenceCommand{
                programs[index], TradeContractArrowPriority, {}});
            commands.push_back(sequence::makeMoveXY(
                id, TradeContractArrowPriority,
                TradeContractArrowX[index], TradeContractArrowY[index]));
        }

        if (commands.size() > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit Trade contract-arrow transition");
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
                    "validated Trade contract-arrow command rejected");
            }
        }

        visible_ = desired;
        return {};
    }
}
