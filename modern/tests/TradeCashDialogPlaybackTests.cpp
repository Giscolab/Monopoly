#include "TradeCashDialogPlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    using namespace monopoly;

    void require(bool condition, std::string_view message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) throw std::runtime_error(std::string(message));
    }

    const engine::SequenceWorld2DObject* object(
        engine::SequencePlayback& playback,
        data::DataId id,
        std::uint16_t priority)
    {
        const auto roots = playback.runtime().matching(id, priority);
        require(roots.size() == 1, "exact Trade cash-dialog DataID/priority has one root");
        return playback.world2D().find(roots.front());
    }

    void testRetailConstants()
    {
        require(tradeui::tradeCashDialogSequence() ==
                data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x118A),
            "cash dialog uses USA DAT_LANG2 TAB_tnmtry00");
        require(tradeui::tradeCashIdleSequence(0) ==
                data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x02D9) &&
                tradeui::tradeCashIdleSequence(1) ==
                data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x02DB) &&
                tradeui::tradeCashIdleSequence(2) ==
                data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x02DD),
            "cash dialog idle buttons use Clear/Okay/Cancel CNKs");
        require(tradeui::TradeCashDialogPriority == 1975 &&
                tradeui::TradeCashButtonPriority == 1976 &&
                tradeui::TradeCashPressedPriority == 1977,
            "cash dialog priorities match DISPLAY_TradeBoxBItemsPriority + 1501/+1502/+1503");
        require(tradeui::tradeCashPressedSequence(tradeui::CashDialogFeedback::Clear) ==
                data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x02DA) &&
                tradeui::tradeCashPressedSequence(tradeui::CashDialogFeedback::Okay) ==
                data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x02DC) &&
                tradeui::tradeCashPressedSequence(tradeui::CashDialogFeedback::Cancel) ==
                data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x02DE),
            "cash dialog pressed feedback uses retail Clear/Okay/Cancel CNKs");
        require(tradeui::TradeCashDialogX == std::array<std::int32_t, 2>{4, 604} &&
                tradeui::TradeCashDialogY == std::array<std::int32_t, 2>{324, 324},
            "cash dialog background positions preserve side A/B retail coordinates");
        require(tradeui::TradeCashButtonX == std::array<std::int32_t, 2>{-306, 294} &&
                tradeui::TradeCashButtonY == std::array<std::int32_t, 2>{-29, -29},
            "cash dialog idle button atlas uses exact legacy offsets");
    }

    void testLifecycleAndSideMove()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        tradeui::CashDialogPlayback dialog;
        tradeui::State state{};

        require(dialog.sync(state, display::Screen2D::Trade, 0, playback).has_value() &&
                playback.commands().pendingCount() == 0 && !dialog.visible(),
            "hidden cash dialog queues no commands");

        state.cashDialogVisible = true;
        state.cashDialogSide = 0;
        require(dialog.sync(state, display::Screen2D::Trade, 0, playback).has_value() &&
                playback.commands().pendingCount() == 8,
            "opening side-A cash dialog queues four Start+Move pairs atomically");
        require(playback.update(0).has_value() && playback.world2D().size() == 4,
            "cash dialog publishes background and three idle buttons");

        const auto* background = object(playback,
            tradeui::tradeCashDialogSequence(), tradeui::TradeCashDialogPriority);
        require(background && background->worldTransform.values[6] == 4.0F &&
                background->worldTransform.values[7] == 324.0F,
            "side-A cash dialog background uses retail (4,324)");
        for (std::size_t index = 0; index < 3; ++index)
        {
            const auto* button = object(playback,
                tradeui::tradeCashIdleSequence(index),
                tradeui::TradeCashButtonPriority);
            require(button && button->worldTransform.values[6] == -306.0F &&
                    button->worldTransform.values[7] == -29.0F,
                "side-A cash idle button uses shared retail atlas offset");
        }

        require(dialog.sync(state, display::Screen2D::Trade, 0, playback).has_value() &&
                playback.commands().pendingCount() == 0,
            "unchanged visible cash dialog queues no redundant commands");

        state.cashDialogSide = 1;
        require(dialog.sync(state, display::Screen2D::Trade, 0, playback).has_value() &&
                playback.commands().pendingCount() == 4,
            "switching cash dialog side moves all four active sequences");
        require(playback.update(1).has_value(),
            "cash dialog side-move commands execute");
        background = object(playback,
            tradeui::tradeCashDialogSequence(), tradeui::TradeCashDialogPriority);
        require(background && background->worldTransform.values[6] == 604.0F &&
                background->worldTransform.values[7] == 324.0F,
            "side-B cash dialog background uses retail (604,324)");
        const auto* okay = object(playback,
            tradeui::tradeCashIdleSequence(1),
            tradeui::TradeCashButtonPriority);
        require(okay && okay->worldTransform.values[6] == 294.0F &&
                okay->worldTransform.values[7] == -29.0F,
            "side-B cash idle buttons use retail atlas offset (294,-29)");

        state.cashDialogVisible = false;
        require(dialog.sync(state, display::Screen2D::Trade, 0, playback).has_value() &&
                playback.commands().pendingCount() == 4,
            "closing cash dialog queues one Stop for every persistent sequence");
        require(playback.update(2).has_value() && playback.world2D().size() == 0 &&
                !dialog.visible(),
            "closing cash dialog removes all four Overlay2D roots");

        state.cashDialogVisible = true;
        state.cashDialogSide = 0;
        require(dialog.sync(state, display::Screen2D::Trade, 0, playback).has_value() &&
                playback.update(3).has_value() && dialog.visible(),
            "cash dialog can reopen after a complete close");
        require(dialog.sync(state, display::Screen2D::Main, 4, playback).has_value() &&
                playback.commands().pendingCount() == 4 &&
                playback.update(4).has_value() && playback.world2D().size() == 0,
            "leaving Trade stops cash dialog even if UI visibility flag remains set");
    }

    void testPressedFeedbackAndDying()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        tradeui::CashDialogPlayback dialog;
        tradeui::State state{};
        state.cashDialogVisible = true;
        state.cashDialogSide = 0;
        require(dialog.sync(state, display::Screen2D::Trade, 10, playback).has_value() &&
                playback.update(10).has_value() && dialog.visible(),
            "cash feedback fixture opens persistent side-A popup");

        state.cashDialogFeedback = tradeui::CashDialogFeedback::Clear;
        require(dialog.sync(state, display::Screen2D::Trade, 11, playback).has_value() &&
                playback.commands().pendingCount() == 3 &&
                state.cashDialogFeedback == tradeui::CashDialogFeedback::None &&
                !state.cashDialogClosing,
            "Clear queues Start/Move/EndingActionStop without entering dying state");
        require(playback.update(11).has_value() &&
                playback.runtime().matching(
                    tradeui::tradeCashPressedSequence(tradeui::CashDialogFeedback::Clear),
                    tradeui::TradeCashPressedPriority).size() == 1,
            "Clear pressed feedback is visible at priority 1977");
        require(playback.update(15).has_value() &&
                playback.runtime().matching(
                    tradeui::tradeCashPressedSequence(tradeui::CashDialogFeedback::Clear),
                    tradeui::TradeCashPressedPriority).empty() &&
                dialog.sync(state, display::Screen2D::Trade, 15, playback).has_value() &&
                dialog.visible() && state.cashDialogVisible && !state.cashDialogClosing,
            "Clear feedback expires after four ticks without closing cash popup");

        state.cashDialogVisible = false;
        state.cashDialogClosing = true;
        state.cashDialogFeedback = tradeui::CashDialogFeedback::Okay;
        require(dialog.sync(state, display::Screen2D::Trade, 20, playback).has_value() &&
                playback.commands().pendingCount() == 3 && dialog.visible() &&
                state.cashDialogClosing,
            "Okay starts pressed feedback while CashTradeDying keeps popup visible");
        require(playback.update(20).has_value() &&
                playback.runtime().matching(
                    tradeui::tradeCashPressedSequence(tradeui::CashDialogFeedback::Okay),
                    tradeui::TradeCashPressedPriority).size() == 1,
            "Okay pressed feedback owns priority 1977 during dying interval");
        require(playback.update(24).has_value() &&
                dialog.sync(state, display::Screen2D::Trade, 24, playback).has_value() &&
                playback.commands().pendingCount() == 4 &&
                !state.cashDialogClosing && !dialog.visible(),
            "Okay completion clears CashTradeDying and queues four persistent Stops");
        require(playback.update(25).has_value() && playback.world2D().size() == 0,
            "Okay dying completion removes complete popup after feedback ends");

        state.cashDialogVisible = true;
        state.cashDialogSide = 1;
        require(dialog.sync(state, display::Screen2D::Trade, 30, playback).has_value() &&
                playback.update(30).has_value() && dialog.visible(),
            "cash popup reopens on side B for Cancel feedback");
        state.cashDialogVisible = false;
        state.cashDialogClosing = true;
        state.cashDialogFeedback = tradeui::CashDialogFeedback::Cancel;
        require(dialog.sync(state, display::Screen2D::Trade, 31, playback).has_value() &&
                playback.update(31).has_value() && state.cashDialogClosing &&
                playback.runtime().matching(
                    tradeui::tradeCashPressedSequence(tradeui::CashDialogFeedback::Cancel),
                    tradeui::TradeCashPressedPriority).size() == 1,
            "Cancel uses its pressed CNK and preserves popup during dying interval");
        require(playback.update(35).has_value() &&
                dialog.sync(state, display::Screen2D::Trade, 35, playback).has_value() &&
                !state.cashDialogClosing && playback.commands().pendingCount() == 4 &&
                playback.update(36).has_value() && playback.world2D().size() == 0,
            "Cancel completion follows same delayed removal contract as retail");
    }

    void testTransactionalFailures()
    {
        tradeui::State state{};
        state.cashDialogVisible = true;
        state.cashDialogSide = 0;

        engine::SequencePlayback missing(nullptr);
        tradeui::CashDialogPlayback missingDialog;
        const auto missingResult =
            missingDialog.sync(state, display::Screen2D::Trade, 0, missing);
        require(!missingResult && missing.commands().pendingCount() == 0 &&
                !missingDialog.visible(),
            "missing cash-dialog resource rejects transition before queueing");

        SyntheticSequenceResources resources;
        engine::SequencePlayback full(resources.service.snapshot());
        tradeui::CashDialogPlayback fullDialog;
        bool filled = true;
        for (std::size_t i = 0;
             i < sequence::SequenceCommandQueue::Capacity - 7; ++i)
        {
            const auto queued = full.commands().enqueue(
                sequence::StopSequenceCommand{data::EmptyDataId, 0, false});
            filled = filled && queued.has_value();
        }
        require(filled, "FIFO fixture leaves only seven slots for eight-command open");
        const auto before = full.commands().pendingCount();
        const auto noRoom =
            fullDialog.sync(state, display::Screen2D::Trade, 0, full);
        require(!noRoom && full.commands().pendingCount() == before &&
                !fullDialog.visible(),
            "insufficient FIFO preserves hidden cash-dialog state transactionally");

        state.cashDialogSide = 2;
        engine::SequencePlayback invalidSide(resources.service.snapshot());
        tradeui::CashDialogPlayback invalidDialog;
        const auto invalid =
            invalidDialog.sync(state, display::Screen2D::Trade, 0, invalidSide);
        require(!invalid && invalidSide.commands().pendingCount() == 0 &&
                !invalidDialog.visible(),
            "invalid cash-dialog side is rejected before resource or queue mutation");
    }
}

int main()
{
    try
    {
        testRetailConstants();
        testLifecycleAndSideMove();
        testPressedFeedbackAndDying();
        testTransactionalFailures();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
