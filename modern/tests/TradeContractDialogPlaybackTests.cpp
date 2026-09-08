#include "TradeContractDialogPlayback.hpp"
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
        data::DataId id)
    {
        const auto roots = playback.runtime().matching(
            id, tradeui::TradeContractArrowPriority);
        require(roots.size() == 1,
            "exact Trade contract-arrow DataID/priority has one root");
        return playback.world2D().find(roots.front());
    }

    void testRetailContract()
    {
        require(tradeui::tradeContractArrowSequence(0) ==
                data::packDataId(data::LegacyGroupId::Main, 0x0009) &&
                tradeui::tradeContractArrowSequence(1) ==
                data::packDataId(data::LegacyGroupId::Main, 0x0007),
            "Future/Immunity arrows use retail CNK_byahaupi/byahadni");
        require(tradeui::TradeContractArrowPriority == 525,
            "Future/Immunity arrows use FutureTradeDlg.priority + 1");
        require(tradeui::TradeContractArrowX ==
                    std::array<std::int32_t, 2>{22, 22} &&
                tradeui::TradeContractArrowY ==
                    std::array<std::int32_t, 2>{-168, -247},
            "Future/Immunity arrows preserve exact retail StartXY positions");
    }

    void testLifecycle()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        tradeui::ContractDialogPlayback arrows;
        tradeui::State state{};

        require(arrows.sync(state, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 0 && !arrows.visible(),
            "hidden Future/Immunity dialog queues no arrow commands");

        state.contractDialogVisible = true;
        state.contractDialogKind = rules::TradeItemKind::FutureRent;
        require(arrows.sync(state, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 4,
            "opening Future dialog queues two Start+Move arrow pairs");
        require(playback.update(0).has_value() && playback.world2D().size() == 2,
            "Future dialog publishes both autonomous arrows into Overlay2D");
        const auto* up = object(playback, tradeui::tradeContractArrowSequence(0));
        const auto* down = object(playback, tradeui::tradeContractArrowSequence(1));
        require(up && up->worldTransform.values[6] == 22.0F &&
                up->worldTransform.values[7] == -168.0F,
            "up arrow uses retail (22,-168)");
        require(down && down->worldTransform.values[6] == 22.0F &&
                down->worldTransform.values[7] == -247.0F,
            "down arrow uses retail (22,-247)");

        state.contractDialogKind = rules::TradeItemKind::Immunity;
        state.contractDialogMode = 6;
        require(arrows.sync(state, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 0,
            "shared arrows do not restart across Future/Immunity mode changes");

        state.contractDialogVisible = false;
        require(arrows.sync(state, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 2,
            "closing contract dialog queues exactly two arrow Stops");
        require(playback.update(1).has_value() && playback.world2D().size() == 0 &&
                !arrows.visible(),
            "closing contract dialog removes both arrows");

        state.contractDialogVisible = true;
        require(arrows.sync(state, display::Screen2D::Trade, playback).has_value() &&
                playback.update(2).has_value() && arrows.visible(),
            "contract arrows can reopen after complete close");
        require(arrows.sync(state, display::Screen2D::Main, playback).has_value() &&
                playback.commands().pendingCount() == 2 &&
                playback.update(3).has_value() && playback.world2D().size() == 0,
            "leaving Trade stops shared contract arrows regardless of UI flag");
    }

    void testTransactionalFailures()
    {
        tradeui::State state{};
        state.contractDialogVisible = true;

        engine::SequencePlayback missing(nullptr);
        tradeui::ContractDialogPlayback missingArrows;
        const auto unavailable =
            missingArrows.sync(state, display::Screen2D::Trade, missing);
        require(!unavailable && missing.commands().pendingCount() == 0 &&
                !missingArrows.visible(),
            "missing arrow resource rejects transition before queueing");

        SyntheticSequenceResources resources;
        engine::SequencePlayback full(resources.service.snapshot());
        tradeui::ContractDialogPlayback fullArrows;
        bool filled = true;
        for (std::size_t i = 0;
             i < sequence::SequenceCommandQueue::Capacity - 3; ++i)
        {
            const auto queued = full.commands().enqueue(
                sequence::StopSequenceCommand{data::EmptyDataId, 0, false});
            filled = filled && queued.has_value();
        }
        require(filled,
            "FIFO fixture leaves only three slots for four-command arrow open");
        const auto before = full.commands().pendingCount();
        const auto noRoom =
            fullArrows.sync(state, display::Screen2D::Trade, full);
        require(!noRoom && full.commands().pendingCount() == before &&
                !fullArrows.visible(),
            "insufficient FIFO preserves hidden contract-arrow state transactionally");
    }
}

int main()
{
    try
    {
        testRetailContract();
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
