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
        require(tradeui::TradePropertyMoveStepMs == 25 &&
                tradeui::TradePropertyMoveSteps == 4,
            "legacy movingcard cadence is four steps with 25 ms timer");
        require(tradeui::TradePropertyMovingPriorities[0] == 626 &&
                tradeui::TradePropertyMovingPriorities[1] == 676 &&
                tradeui::TradePropertyMovingPriorities[2] == 726 &&
                tradeui::TradePropertyMovingPriorities[3] == 776,
            "legacy movingcard priorities are exact for all four Trade boxes");
    }

    void testStaticLifecycleAndAnimatedMoveBetweenBoxes()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        tradeui::PropertyPlayback deeds;
        auto game = propertyGame();
        tradeui::State state{};
        require(tradeui::beginLocalTrade(state, game, 0),
            "Trade deed playback fixture initializes local trade");

        require(deeds.sync(state, game, display::Screen2D::Trade, 0, playback).has_value() &&
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

        const auto beforeProjection = tradeui::projectProperties(state, game);
        const auto source5 = beforeProjection.hitRects[0][5];
        uimsg::Message click{};
        click.type = uimsg::Type::MouseLeftDown;
        click.numberA = source5.left + 1;
        click.numberB = source5.top + 1;
        (void)tradeui::processInput(state, game, display::Screen2D::Trade, click);

        require(state.propertyMove &&
                state.propertyMove->square == 5 &&
                state.propertyMove->fromBox == 0 &&
                state.propertyMove->toBox == 2 &&
                state.propertyMove->from == source5,
            "local property click records exact before-A to offer-A move request");

        const auto offeredProjection = tradeui::projectProperties(state, game);
        const auto offered5 = offeredProjection.hitRects[2][5];
        const auto offeredPriority = static_cast<std::uint16_t>(
            tradeui::TradePropertyBasePriorities[2] +
            offeredProjection.priorities[2][5]);
        require(state.propertyMove->to == offered5,
            "move request captures final projected destination before playback");

        const auto blocked15 = offeredProjection.hitRects[0][15];
        const auto itemCountWhileMoving = state.items.size();
        click.numberA = blocked15.left + 1;
        click.numberB = blocked15.top + 1;
        (void)tradeui::processInput(state, game, display::Screen2D::Trade, click);
        require(state.items.size() == itemCountWhileMoving &&
                state.propertyMove && state.propertyMove->square == 5,
            "property clicks are blocked while movingcard playback is active");

        const int forwardDx = (offered5.left - source5.left) / 4;
        const int forwardDy = (offered5.top - source5.top) / 4;
        require(deeds.sync(state, game, display::Screen2D::Trade, 100, playback).has_value() &&
                playback.update(100).has_value(),
            "offering square 5 starts movingcard transaction");
        require(playback.runtime().matching(normal5, 356).empty() &&
                playback.runtime().matching(normal5, offeredPriority).empty(),
            "moving deed replaces source static root and suppresses final static root");
        const auto* moving = object(playback, normal5, 726);
        require(moving &&
                moving->worldTransform.values[6] == static_cast<float>(source5.left + forwardDx) &&
                moving->worldTransform.values[7] == static_cast<float>(source5.top + forwardDy),
            "A-to-offer movingcard starts directly at one-quarter with priority 726");

        require(deeds.sync(state, game, display::Screen2D::Trade, 124, playback).has_value() &&
                playback.commands().pendingCount() == 0,
            "movingcard does not advance before 25 ms");
        require(deeds.sync(state, game, display::Screen2D::Trade, 125, playback).has_value() &&
                playback.update(125).has_value(),
            "movingcard advances to two-quarters at 25 ms");
        moving = object(playback, normal5, 726);
        require(moving &&
                moving->worldTransform.values[6] == static_cast<float>(source5.left + 2 * forwardDx) &&
                moving->worldTransform.values[7] == static_cast<float>(source5.top + 2 * forwardDy),
            "second movingcard position matches integer legacy delta");

        require(deeds.sync(state, game, display::Screen2D::Trade, 150, playback).has_value() &&
                playback.update(150).has_value(),
            "movingcard advances to three-quarters at 50 ms");
        moving = object(playback, normal5, 726);
        require(moving &&
                moving->worldTransform.values[6] == static_cast<float>(source5.left + 3 * forwardDx) &&
                moving->worldTransform.values[7] == static_cast<float>(source5.top + 3 * forwardDy),
            "third movingcard position matches integer legacy delta");

        require(deeds.sync(state, game, display::Screen2D::Trade, 175, playback).has_value() &&
                playback.update(175).has_value() &&
                !state.propertyMove,
            "fourth movingcard step stops transient deed and commits static final state");
        require(playback.runtime().matching(normal5, 726).empty(),
            "completed A-to-offer movingcard root is removed");
        const auto* offeredObject = object(playback, normal5, offeredPriority);
        require(offeredObject &&
                offeredObject->worldTransform.values[6] == static_cast<float>(offered5.left) &&
                offeredObject->worldTransform.values[7] == static_cast<float>(offered5.top),
            "offered deed appears statically only after movingcard completion");

        click.numberA = offered5.left + 1;
        click.numberB = offered5.top + 1;
        (void)tradeui::processInput(state, game, display::Screen2D::Trade, click);
        require(state.propertyMove &&
                state.propertyMove->fromBox == 2 &&
                state.propertyMove->toBox == 0,
            "clicking offered deed records exact reverse move request");

        const auto returnedProjection = tradeui::projectProperties(state, game);
        const auto returned5 = returnedProjection.hitRects[0][5];
        const int reverseDx = (returned5.left - offered5.left) / 4;
        const int reverseDy = (returned5.top - offered5.top) / 4;
        require(deeds.sync(state, game, display::Screen2D::Trade, 200, playback).has_value() &&
                playback.update(200).has_value(),
            "removing offered deed starts reverse movingcard transaction");
        moving = object(playback, normal5, 626);
        require(moving &&
                moving->worldTransform.values[6] == static_cast<float>(offered5.left + reverseDx) &&
                moving->worldTransform.values[7] == static_cast<float>(offered5.top + reverseDy),
            "offer-to-A movingcard starts at one-quarter with priority 626");

        require(deeds.sync(state, game, display::Screen2D::Trade, 225, playback).has_value() &&
                playback.update(225).has_value() &&
                deeds.sync(state, game, display::Screen2D::Trade, 250, playback).has_value() &&
                playback.update(250).has_value() &&
                deeds.sync(state, game, display::Screen2D::Trade, 275, playback).has_value() &&
                playback.update(275).has_value() &&
                !state.propertyMove,
            "reverse movingcard completes through two, three and final four-quarter steps");
        require(playback.runtime().matching(normal5, 626).empty(),
            "completed reverse movingcard root is removed");
        deed5 = object(playback, normal5, 356);
        require(deed5 &&
                deed5->worldTransform.values[6] == static_cast<float>(returned5.left) &&
                deed5->worldTransform.values[7] == static_cast<float>(returned5.top),
            "returned deed restores original before-A static slot");

        require(deeds.sync(state, game, display::Screen2D::Trade, 300, playback).has_value() &&
                playback.commands().pendingCount() == 0,
            "unchanged Trade deed projection queues no redundant commands");

        require(deeds.sync(state, game, display::Screen2D::Main, 325, playback).has_value() &&
                playback.commands().pendingCount() == 2 &&
                playback.update(325).has_value() && playback.world2D().size() == 0,
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
            missingDeeds.sync(state, game, display::Screen2D::Trade, 0, missing);
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
            fullDeeds.sync(state, game, display::Screen2D::Trade, 0, full);
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
        testStaticLifecycleAndAnimatedMoveBetweenBoxes();
        testTransactionalFailures();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
