#include "TradeCashDialogPlayback.hpp"

#include <array>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::tradeui
{
    namespace
    {
        inline constexpr std::size_t CashDialogSequenceCount = 4;
        inline constexpr std::uint8_t EndingActionStop = 1;

        [[nodiscard]] std::array<data::DataId, CashDialogSequenceCount>
        cashDialogSequences() noexcept
        {
            return {
                tradeCashDialogSequence(),
                tradeCashIdleSequence(0),
                tradeCashIdleSequence(1),
                tradeCashIdleSequence(2)};
        }

        [[nodiscard]] std::uint16_t cashDialogPriority(
            std::size_t index) noexcept
        {
            return index == 0 ? TradeCashDialogPriority : TradeCashButtonPriority;
        }

        [[nodiscard]] std::int32_t cashDialogX(
            std::size_t index,
            std::uint8_t side) noexcept
        {
            return index == 0 ? TradeCashDialogX[side] : TradeCashButtonX[side];
        }

        [[nodiscard]] std::int32_t cashDialogY(
            std::size_t index,
            std::uint8_t side) noexcept
        {
            return index == 0 ? TradeCashDialogY[side] : TradeCashButtonY[side];
        }
    }

    std::expected<void, std::string> CashDialogPlayback::sync(
        State& state,
        display::Screen2D desiredView,
        std::uint64_t nowTick,
        engine::SequencePlayback& playback)
    {
        const bool tradeView = desiredView == display::Screen2D::Trade;
        if (tradeView && (state.cashDialogVisible || state.cashDialogClosing) &&
            state.cashDialogSide >= 2)
        {
            return std::unexpected("Trade cash-dialog side is out of range");
        }

        const bool feedbackCompleted =
            feedback_ != data::EmptyDataId &&
            nowTick > feedbackStartTick_ &&
            playback.runtime().matching(
                feedback_, TradeCashPressedPriority).empty();
        const auto activeFeedback = feedbackCompleted
            ? data::EmptyDataId
            : feedback_;

        bool closingAfter = state.cashDialogClosing;
        if (!tradeView)
            closingAfter = false;
        else if (feedbackCompleted &&
                 state.cashDialogFeedback == CashDialogFeedback::None)
            closingAfter = false;

        const bool desiredVisible = tradeView &&
            (state.cashDialogVisible || closingAfter);
        const auto desiredSide = desiredVisible
            ? state.cashDialogSide
            : side_;

        const auto requestedFeedback = tradeView
            ? state.cashDialogFeedback
            : CashDialogFeedback::None;
        const auto requestedFeedbackId =
            tradeCashPressedSequence(requestedFeedback);
        if (requestedFeedback != CashDialogFeedback::None &&
            requestedFeedbackId == data::EmptyDataId)
        {
            return std::unexpected("Trade cash-dialog feedback is out of range");
        }

        const auto ids = cashDialogSequences();
        std::array<std::shared_ptr<const sequence::SequenceProgram>,
            CashDialogSequenceCount> programs{};
        if (desiredVisible && !visible_)
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

        std::shared_ptr<const sequence::SequenceProgram> feedbackProgram;
        if (requestedFeedbackId != data::EmptyDataId)
        {
            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), requestedFeedbackId);
            if (!loaded)
                return std::unexpected(loaded.error().detail);
            feedbackProgram = std::move(*loaded);
        }

        std::vector<sequence::SequenceCommand> commands;
        if (visible_ && !desiredVisible)
        {
            for (std::size_t index = 0; index < ids.size(); ++index)
                commands.push_back(sequence::StopSequenceCommand{
                    ids[index], cashDialogPriority(index), false});
        }
        else if (!visible_ && desiredVisible)
        {
            for (std::size_t index = 0; index < ids.size(); ++index)
            {
                commands.push_back(sequence::StartSequenceCommand{
                    programs[index], cashDialogPriority(index), {}});
                commands.push_back(sequence::makeMoveXY(
                    ids[index], cashDialogPriority(index),
                    cashDialogX(index, desiredSide),
                    cashDialogY(index, desiredSide)));
            }
        }
        else if (visible_ && desiredVisible && side_ != desiredSide)
        {
            for (std::size_t index = 0; index < ids.size(); ++index)
            {
                commands.push_back(sequence::makeMoveXY(
                    ids[index], cashDialogPriority(index),
                    cashDialogX(index, desiredSide),
                    cashDialogY(index, desiredSide)));
            }
        }

        if (requestedFeedbackId != data::EmptyDataId)
        {
            if (activeFeedback != data::EmptyDataId)
            {
                commands.push_back(sequence::StopSequenceCommand{
                    activeFeedback, TradeCashPressedPriority, false});
            }
            commands.push_back(sequence::StartSequenceCommand{
                feedbackProgram, TradeCashPressedPriority, {}});
            commands.push_back(sequence::makeMoveXY(
                requestedFeedbackId, TradeCashPressedPriority,
                TradeCashButtonX[state.cashDialogSide],
                TradeCashButtonY[state.cashDialogSide]));
            commands.push_back(sequence::SetSequenceEndingActionCommand{
                requestedFeedbackId, TradeCashPressedPriority,
                EndingActionStop, false});
        }

        if (commands.size() > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit Trade cash-dialog transition");
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
                    "validated Trade cash-dialog command rejected");
            }
        }

        visible_ = desiredVisible;
        if (desiredVisible)
            side_ = desiredSide;

        if (!tradeView)
        {
            state.cashDialogVisible = false;
            state.cashDialogClosing = false;
            state.cashDialogFeedback = CashDialogFeedback::None;
            if (feedbackCompleted)
            {
                feedback_ = data::EmptyDataId;
                feedbackStartTick_ = 0;
            }
        }
        else
        {
            state.cashDialogClosing = closingAfter;
            if (requestedFeedbackId != data::EmptyDataId)
            {
                feedback_ = requestedFeedbackId;
                feedbackStartTick_ = nowTick;
                state.cashDialogFeedback = CashDialogFeedback::None;
            }
            else if (feedbackCompleted)
            {
                feedback_ = data::EmptyDataId;
                feedbackStartTick_ = 0;
            }
        }

        return {};
    }
}
