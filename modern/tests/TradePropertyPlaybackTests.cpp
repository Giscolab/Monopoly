#include "TradePropertyPlayback.hpp"
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

    rules::GameState propertyGame()
    {
        rules::GameState game{};
        game.numberOfPlayers = 2;
        game.players[0].currentSquare = 0;
        game.players[1].currentSquare = 1;
        game.players[0].cash = 1500;
        game.players[1].cash = 1500;
        game.squares[5].owner = 0;
        game.squares[15].owner = 0;
        game.squares[15].mortgaged = true;
        return game;
    }

    sequence::SequenceNodeId root(
        engine::SequencePlayback& playback,
        data::DataId id,
        std::uint16_t priority)
    {
        const auto roots = playback.runtime().matching(id, priority);
        require(roots.size() == 1, "exact trade deed DataID/priority has one root");
        return roots.front();
    }

    const engine::SequenceWorld2DObject* object(
        engine::SequencePlayback& playback,
        data::DataId id,
        std::uint16_t priority)
    {
        return playback.world2D().find(root(playback, id, priority));
    }

    void testDataIds()
    {
        require(tradeui::tradePropertyDataId(1, false) ==
                data::packDataId(data::LegacyGroupId::Patterns, 0x05E6),
            "Mediterranean normal Trade deed starts at retail TAB_trprfp0000");
        require(tradeui::tradePropertyDataId(39, true) ==
                data::packDataId(data::LegacyGroupId::Patterns, 0x05E5),
            "Boardwalk mortgaged Trade deed ends at retail TAB_trprfm0027");
        require(tradeui::tradePropertyDataId(0, false) == data::EmptyDataId,
            "non-ownable square has no Trade deed DataID");
    }

    void testStaticLifecycleAndMoveBetweenBoxes()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        tradeui::PropertyPlayback deeds;
        auto game = propertyGame();
        tradeui::State state{};
        require(tradeui::beginLocalTrade(state, game, 0),
            "Trade deed playback fixture initializes local trade");

        require(deeds.sync(state, game, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 4,
            "two initial deeds queue Start+Move pairs atomically");
        require(playback.update(0).has_value() && playback.world2D().size() == 2,
            "two initial Trade deeds publish into Overlay2D");

        const auto normal5 = tradeui::tradePropertyDataId(5, false);
        const auto mortgaged15 = tradeui::tradePropertyDataId(15, true);
        const auto* deed5 = object(playback, normal5, 356);
        const auto* deed15 = object(playback, mortgaged15, 354);
        require(deed5 && deed5->worldTransform.values[6] == 17.0F &&
                deed5->worldTransform.values[7] == 285.0F,
            "before-A square 5 uses priority 324+32 and retail (17,285)");
        require(deed15 && deed15->worldTransform.values[6] == 11.0F &&
                deed15->worldTransform.values[7] == 265.0F,
            "mortgaged square 15 uses priority 324+30 and retail (11,265)");

        uimsg::Message click{};
        click.type = uimsg::Type::MouseLeftDown;
        click.numberA = 20;
        click.numberB = 290;
        (void)tradeui::processInput(state, game, display::Screen2D::Trade, click);
        auto projection = tradeui::projectProperties(state, game);
        const auto offered5 = projection.hitRects[2][5];
        const auto offeredPriority = static_cast<std::uint16_t>(
            tradeui::TradePropertyBasePriorities[2] + projection.priorities[2][5]);

        require(deeds.sync(state, game, display::Screen2D::Trade, playback).has_value() &&
                playback.update(1).has_value(),
            "offering square 5 commits static deed transition");
        require(playback.runtime().matching(normal5, 356).empty(),
            "offering deed stops its before-A root");
        const auto* offeredObject = object(playback, normal5, offeredPriority);
        require(offeredObject &&
                offeredObject->worldTransform.values[6] == static_cast<float>(offered5.left) &&
                offeredObject->worldTransform.values[7] == static_cast<float>(offered5.top),
            "offered deed restarts at Trade-A item priority and projected coordinates");

        click.numberA = offered5.left + 1;
        click.numberB = offered5.top + 1;
        (void)tradeui::processInput(state, game, display::Screen2D::Trade, click);
        require(deeds.sync(state, game, display::Screen2D::Trade, playback).has_value() &&
                playback.update(2).has_value(),
            "removing offered deed commits reverse static transition");
        deed5 = object(playback, normal5, 356);
        require(deed5 && deed5->worldTransform.values[6] == 17.0F &&
                deed5->worldTransform.values[7] == 285.0F,
            "returned deed restores original before-A slot");

        require(deeds.sync(state, game, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 0,
            "unchanged Trade deed projection queues no redundant commands");

        require(deeds.sync(state, game, display::Screen2D::Main, playback).has_value() &&
                playback.commands().pendingCount() == 2 &&
                playback.update(3).has_value() && playback.world2D().size() == 0,
            "leaving Trade stops every remaining static deed");
    }

    void testTransactionalFailures()
    {
        auto game = propertyGame();
        tradeui::State state{};
        require(tradeui::beginLocalTrade(state, game, 0),
            "transactional deed fixture initializes local trade");

        engine::SequencePlayback missing(nullptr);
        tradeui::PropertyPlayback missingDeeds;
        const auto unavailable =
            missingDeeds.sync(state, game, display::Screen2D::Trade, missing);
        require(!unavailable && missing.commands().pendingCount() == 0 &&
                missing.world2D().size() == 0,
            "missing PAT deed resource rejects complete transition before queueing");

        SyntheticSequenceResources resources;
        engine::SequencePlayback full(resources.service.snapshot());
        tradeui::PropertyPlayback fullDeeds;
        for (std::size_t count = 0;
             count < sequence::SequenceCommandQueue::Capacity - 1; ++count)
        {
            if (!full.commands().enqueue(
                    sequence::StopSequenceCommand{1, 0, false}))
                throw std::runtime_error("Trade deed FIFO setup failed");
        }
        const auto pendingBefore = full.commands().pendingCount();
        const auto noRoom =
            fullDeeds.sync(state, game, display::Screen2D::Trade, full);
        require(!noRoom && full.commands().pendingCount() == pendingBefore &&
                full.world2D().size() == 0,
            "insufficient FIFO preserves empty Trade deed playback state transactionally");
    }
}

int main()
{
    try
    {
        testDataIds();
        testStaticLifecycleAndMoveBetweenBoxes();
        testTransactionalFailures();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
