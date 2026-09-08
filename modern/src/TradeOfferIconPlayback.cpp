#include "TradeOfferIconPlayback.hpp"

#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::tradeui
{
    namespace
    {
        struct IconSpec
        {
            data::DataId id{data::EmptyDataId};
            std::uint16_t priority{};
            std::int32_t x{};
            std::int32_t y{};
            bool desired{};
        };

        using Specs = std::array<IconSpec, 16>;

        [[nodiscard]] Specs desiredIcons(
            const State& state,
            display::Screen2D desiredView) noexcept
        {
            Specs specs{};
            const bool tradeVisible = desiredView == display::Screen2D::Trade;
            for (std::size_t slot = 0; slot < TradeIconSlots; ++slot)
            {
                const auto priority = static_cast<std::uint16_t>(
                    TradeIconBasePriority + slot + 1);
                specs[slot] = {
                    tradeJailIcon(0), priority,
                    TradeChanceX[slot], TradeChanceY[slot],
                    tradeVisible && (state.jailCardDesired[0] & (1u << slot)) != 0};
                specs[TradeIconSlots + slot] = {
                    tradeJailIcon(1), priority,
                    TradeCommunityX[slot], TradeCommunityY[slot],
                    tradeVisible && (state.jailCardDesired[1] & (1u << slot)) != 0};
                specs[2 * TradeIconSlots + slot] = {
                    tradeContractIcon(0), priority,
                    TradeFutureX[slot], TradeFutureY[slot],
                    tradeVisible && (state.immunityFutureDesired[0] & (1u << slot)) != 0};
                specs[3 * TradeIconSlots + slot] = {
                    tradeContractIcon(1), priority,
                    TradeImmunityX[slot], TradeImmunityY[slot],
                    tradeVisible && (state.immunityFutureDesired[1] & (1u << slot)) != 0};
            }
            return specs;
        }
    }

    std::expected<void, std::string> OfferIconPlayback::sync(
        const State& state,
        display::Screen2D desiredView,
        engine::SequencePlayback& playback)
    {
        const auto specs = desiredIcons(state, desiredView);
        std::array<std::shared_ptr<const sequence::SequenceProgram>, 16> programs{};

        for (std::size_t index = 0; index < specs.size(); ++index)
        {
            if (!specs[index].desired || current_[index])
                continue;
            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), specs[index].id);
            if (!loaded)
                return std::unexpected(loaded.error().detail);
            programs[index] = std::move(*loaded);
        }

        std::vector<sequence::SequenceCommand> commands;
        for (std::size_t index = 0; index < specs.size(); ++index)
        {
            if (specs[index].desired == current_[index])
                continue;
            const auto& spec = specs[index];
            if (!spec.desired)
            {
                commands.push_back(sequence::StopSequenceCommand{
                    spec.id, spec.priority, false});
            }
            else
            {
                commands.push_back(sequence::StartSequenceCommand{
                    programs[index], spec.priority, {}});
                commands.push_back(sequence::makeMoveXY(
                    spec.id, spec.priority, spec.x, spec.y));
            }
        }

        if (commands.size() > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit Trade offer-icon transition");
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
                return std::unexpected("validated Trade offer-icon command rejected");
        }

        for (std::size_t index = 0; index < specs.size(); ++index)
            current_[index] = specs[index].desired;
        return {};
    }
}
