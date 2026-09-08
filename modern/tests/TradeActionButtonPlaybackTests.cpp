#include "TradeActionButtonPlayback.hpp"
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

    void testRetailConstants()
    {
        require(tradeui::tradeActionSequence(tradeui::TradeCancelIdleTag) ==
                data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x01AF),
            "Trade Cancel idle uses retail CNK_iytcxlf");
        require(tradeui::tradeActionSequence(tradeui::TradeCancelOutTag) ==
                data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x01B1),
            "Trade Cancel out uses retail CNK_iytcxlo");
        require(tradeui::tradeActionSequence(tradeui::TradeProposeIdleTag) ==
                data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x01B8),
            "Trade Propose idle uses retail CNK_iytprpf");
        require(tradeui::tradeActionSequence(tradeui::TradeProposeOutTag) ==
                data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x01BA),
            "Trade Propose out uses retail CNK_iytprpo");
        require(tradeui::TradeCancelPriority == 201 &&
                tradeui::TradeProposePriority == 202,
            "Trade action-button priorities match retail values");
        require(tradeui::TradeActionEndingStop == 1,
            "Trade action-button out animations use EndingActionStop");
    }

    void testLifecycle()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        tradeui::ActionButtonPlayback buttons;
        tradeui::State state{};
        state.showPropose = true;

        require(buttons.sync(state, display::Screen2D::Main, playback).has_value() &&
                playback.commands().pendingCount() == 0 && !buttons.visible(),
            "off-view Trade action buttons queue nothing");
        require(buttons.sync(state, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 4 && buttons.visible(),
            "opening Trade action buttons queues two Start+Move pairs");
        require(playback.update(0).has_value() && playback.world2D().size() == 2,
            "opening publishes two Trade action-button roots");
        const auto proposeIdle = tradeui::tradeActionSequence(tradeui::TradeProposeIdleTag);
        const auto cancelIdle = tradeui::tradeActionSequence(tradeui::TradeCancelIdleTag);
        require(playback.runtime().matching(proposeIdle,
                    tradeui::TradeProposePriority).size() == 1 &&
                playback.runtime().matching(cancelIdle,
                    tradeui::TradeCancelPriority).size() == 1,
            "Trade Propose and Cancel idle roots use exact retail priorities");
        require(buttons.sync(state, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 0,
            "unchanged Trade action buttons queue no redundant commands");

        state.showPropose = false;
        require(buttons.sync(state, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 8 && !buttons.visible(),
            "hiding Trade action buttons queues idle Stop and finite out feedback");
        require(playback.update(1).has_value(),
            "Trade action-button out transition executes");
        const auto proposeOut = tradeui::tradeActionSequence(tradeui::TradeProposeOutTag);
        const auto cancelOut = tradeui::tradeActionSequence(tradeui::TradeCancelOutTag);
        require(playback.runtime().matching(proposeIdle,
                    tradeui::TradeProposePriority).empty() &&
                playback.runtime().matching(cancelIdle,
                    tradeui::TradeCancelPriority).empty(),
            "idle Trade action buttons are removed before out feedback");
        require(playback.runtime().matching(proposeOut,
                    tradeui::TradeProposePriority).size() == 1 &&
                playback.runtime().matching(cancelOut,
                    tradeui::TradeCancelPriority).size() == 1,
            "finite Trade action-button out roots are active after transition");
        require(playback.update(6).has_value() &&
                playback.runtime().matching(proposeOut,
                    tradeui::TradeProposePriority).empty() &&
                playback.runtime().matching(cancelOut,
                    tradeui::TradeCancelPriority).empty(),
            "EndingActionStop removes Trade action-button out roots at natural end");

        state.showPropose = true;
        require(buttons.sync(state, display::Screen2D::Trade, playback).has_value() &&
                playback.update(7).has_value() && buttons.visible(),
            "Trade action buttons can reopen after completed out feedback");
        require(buttons.sync(state, display::Screen2D::Main, playback).has_value() &&
                playback.commands().pendingCount() == 8 && !buttons.visible(),
            "leaving Trade animates both visible action buttons out");
    }

    void testTransactionalFailures()
    {
        tradeui::State state{};
        state.showPropose = true;
        engine::SequencePlayback missing(nullptr);
        tradeui::ActionButtonPlayback missingButtons;
        const auto unavailable = missingButtons.sync(
            state, display::Screen2D::Trade, missing);
        require(!unavailable && missing.commands().pendingCount() == 0 &&
                !missingButtons.visible(),
            "missing Trade action-button resources reject before queueing");
        SyntheticSequenceResources resources;
        engine::SequencePlayback full(resources.service.snapshot());
        tradeui::ActionButtonPlayback fullButtons;
        bool filled = true;
        for (std::size_t index = 0;
             index < sequence::SequenceCommandQueue::Capacity - 3; ++index)
        {
            const auto queued = full.commands().enqueue(
                sequence::StopSequenceCommand{data::EmptyDataId, 0, false});
            filled = filled && queued.has_value();
        }
        require(filled,
            "FIFO fixture leaves only three slots for four-command Trade button open");
        const auto before = full.commands().pendingCount();
        const auto noRoom = fullButtons.sync(
            state, display::Screen2D::Trade, full);
        require(!noRoom && full.commands().pendingCount() == before &&
                !fullButtons.visible(),
            "insufficient FIFO preserves hidden Trade action-button state");
    }
}

int main()
{
    try
    {
        testRetailConstants();
        testLifecycle();
        testTransactionalFailures();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
