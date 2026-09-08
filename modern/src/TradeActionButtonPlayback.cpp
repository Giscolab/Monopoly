#include "TradeActionButtonPlayback.hpp"

#include <array>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::tradeui
{
    namespace
    {
        struct ButtonSpec
        {
            data::DataId idle{};
            data::DataId out{};
            std::uint16_t priority{};
        };

        [[nodiscard]] constexpr std::array<ButtonSpec, 2>
        buttonSpecs() noexcept
        {
            return {{
                {tradeActionSequence(TradeProposeIdleTag),
                 tradeActionSequence(TradeProposeOutTag),
                 TradeProposePriority},
                {tradeActionSequence(TradeCancelIdleTag),
                 tradeActionSequence(TradeCancelOutTag),
                 TradeCancelPriority}}};
        }
    }

    std::expected<void, std::string> ActionButtonPlayback::sync(
        const State& state,
        display::Screen2D desiredView,
        engine::SequencePlayback& playback)
    {
        const bool desiredVisible =
            desiredView == display::Screen2D::Trade && state.showPropose;
        if (desiredVisible == visible_)
            return {};

        const auto specs = buttonSpecs();
        std::array<std::shared_ptr<const sequence::SequenceProgram>, 2> programs{};
        for (std::size_t index = 0; index < specs.size(); ++index)
        {
            const auto id = desiredVisible ? specs[index].idle : specs[index].out;
            auto loaded = sequence::SequenceProgram::load(playback.resources(), id);
            if (!loaded)
                return std::unexpected(loaded.error().detail);
            programs[index] = std::move(*loaded);
        }

        std::vector<sequence::SequenceCommand> commands;
        commands.reserve(desiredVisible ? 4U : 8U);

        for (std::size_t index = 0; index < specs.size(); ++index)
        {
            const auto& spec = specs[index];
            if (desiredVisible)
            {
                commands.push_back(sequence::StartSequenceCommand{
                    programs[index], spec.priority, {}});
                commands.push_back(sequence::makeMoveXY(
                    spec.idle, spec.priority, 0, 0));
            }
            else
            {
                commands.push_back(sequence::StopSequenceCommand{
                    spec.idle, spec.priority, false});
                commands.push_back(sequence::StartSequenceCommand{
                    programs[index], spec.priority, {}});
                commands.push_back(sequence::makeMoveXY(
                    spec.out, spec.priority, 0, 0));
                commands.push_back(sequence::SetSequenceEndingActionCommand{
                    spec.out, spec.priority, TradeActionEndingStop, false});
            }
        }

        if (commands.size() > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit Trade action-button transition");
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
                    "validated Trade action-button command rejected");
        }

        visible_ = desiredVisible;
        return {};
    }
}
