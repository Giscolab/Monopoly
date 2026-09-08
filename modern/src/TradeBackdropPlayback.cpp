#include "TradeBackdropPlayback.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::tradeui
{
    namespace
    {
        struct BackdropSpec
        {
            data::DataId id{data::EmptyDataId};
            std::uint16_t priority{};
            std::int32_t x{};
            std::int32_t y{};
            bool loop{};
        };

        using Specs = std::array<BackdropSpec, 11>;

        [[nodiscard]] std::expected<data::DataId, std::string> colourRail(
            const State& state,
            const rules::GameState& gameState,
            bool right)
        {
            const auto player = right ? state.playerB : state.playerA;
            const auto base = right ? TradeRightColourBaseTag : TradeLeftColourBaseTag;
            if (player >= rules::MaxPlayers)
                return tradeBackdropSequence(static_cast<data::DataTag>(base + 6u));

            const auto colour = gameState.players[player].colour;
            if (colour > 5)
                return std::unexpected("Trade colour rail player colour is out of range");

            return tradeBackdropSequence(
                static_cast<data::DataTag>(base + colour));
        }

        [[nodiscard]] std::expected<Specs, std::string> desiredBackdrop(
            const State& state,
            const rules::GameState& gameState,
            display::Screen2D desiredView)
        {
            Specs specs{};
            specs[0].priority = TradeBackdropPriority;
            specs[1].priority = TradeDisplayBoardPriority;
            specs[2].priority = TradeTurntablePriority;
            specs[3].priority = TradeArrowPriority;
            specs[4].priority = TradeDisplayPanelPriority;
            specs[5].priority = TradeDisplayPanelPriority;
            specs[6].priority = TradeColourRailPriority;
            specs[7].priority = TradeColourRailPriority;
            specs[8].priority = TradeCreateButtonPriority;
            specs[9].priority = TradeFutureButtonPriority;
            specs[10].priority = TradeImmunityButtonPriority;
            const bool tradeVisible = desiredView == display::Screen2D::Trade;
            if (!tradeVisible)
                return specs;

            const auto leftRail = colourRail(state, gameState, false);
            if (!leftRail) return std::unexpected(leftRail.error());
            const auto rightRail = colourRail(state, gameState, true);
            if (!rightRail) return std::unexpected(rightRail.error());
            specs[0] = {tradeBackdropSequence(TradeBackgroundTag),
                TradeBackdropPriority, 0, 0, false};
            specs[1] = {tradeBackdropSequence(TradeDisplayBoardTag),
                TradeDisplayBoardPriority, 0, 0, false};
            specs[2] = {tradeBackdropSequence(TradeTurntableTag),
                TradeTurntablePriority, 1, 0, false};
            specs[3] = {tradeBackdropSequence(TradeArrowTag),
                TradeArrowPriority, 0, 0, false};
            specs[4] = {tradeBackdropSequence(TradeDisplayLeftTag),
                TradeDisplayPanelPriority, 0, 0, false};
            specs[5] = {tradeBackdropSequence(TradeDisplayRightTag),
                TradeDisplayPanelPriority, 0, 0, false};
            specs[6] = {*leftRail, TradeColourRailPriority,
                state.playerA >= rules::MaxPlayers ? 1 : 0, 0, false};
            specs[7] = {*rightRail, TradeColourRailPriority, 0, 0, false};

            if (gameState.options.futureRentTradingAllowed ||
                gameState.options.immunitiesTradingAllowed)
                specs[8] = {tradeBackdropSequence(TradeCreateButtonTag),
                    TradeCreateButtonPriority, 0, 0, true};
            if (gameState.options.futureRentTradingAllowed)
                specs[9] = {tradeBackdropSequence(TradeFutureButtonTag),
                    TradeFutureButtonPriority, 0, 0, true};
            if (gameState.options.immunitiesTradingAllowed)
                specs[10] = {tradeBackdropSequence(TradeImmunityButtonTag),
                    TradeImmunityButtonPriority, 0, 0, true};
            return specs;
        }
    }

    std::size_t BackdropPlayback::activeCount() const noexcept
    {
        return static_cast<std::size_t>(std::count_if(
            current_.begin(), current_.end(),
            [](data::DataId id) { return id != data::EmptyDataId; }));
    }

    std::expected<void, std::string> BackdropPlayback::sync(
        const State& state,
        const rules::GameState& gameState,
        display::Screen2D desiredView,
        engine::SequencePlayback& playback)
    {
        const auto desiredResult = desiredBackdrop(state, gameState, desiredView);
        if (!desiredResult) return std::unexpected(desiredResult.error());
        const auto& specs = *desiredResult;
        std::array<std::shared_ptr<const sequence::SequenceProgram>, 11> programs{};
        for (std::size_t index = 0; index < specs.size(); ++index)
        {
            if (specs[index].id == data::EmptyDataId ||
                specs[index].id == current_[index])
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
            if (current_[index] == specs[index].id)
                continue;
            if (current_[index] != data::EmptyDataId)
                commands.push_back(sequence::StopSequenceCommand{
                    current_[index], specs[index].priority, false});
        }

        for (std::size_t index = 0; index < specs.size(); ++index)
        {
            if (specs[index].id == data::EmptyDataId ||
                current_[index] == specs[index].id)
                continue;
            commands.push_back(sequence::StartSequenceCommand{
                programs[index], specs[index].priority, {}});
            commands.push_back(sequence::makeMoveXY(
                specs[index].id, specs[index].priority,
                specs[index].x, specs[index].y));
            if (specs[index].loop)
                commands.push_back(sequence::SetSequenceEndingActionCommand{
                    specs[index].id, specs[index].priority,
                    TradeLoopToBeginning, false});
        }

        if (commands.size() > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
            return std::unexpected(
                "sequence command queue cannot fit Trade backdrop transition");

        for (auto& command : commands)
        {
            const auto queued = std::visit(
                [&](auto value)
                {
                    return playback.commands().enqueue(std::move(value));
                },
                std::move(command));
            if (!queued)
                return std::unexpected("validated Trade backdrop command rejected");
        }

        for (std::size_t index = 0; index < specs.size(); ++index)
            current_[index] = specs[index].id;
        return {};
    }
}
