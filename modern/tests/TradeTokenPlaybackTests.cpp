#include "TradeTokenPlayback.hpp"
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
        require(roots.size() == 1,
            "exact Trade token DataID/priority has one root");
        return playback.world2D().find(roots.front());
    }

    void testRetailConstants()
    {
        require(tradeui::tradeTokenSequence(0) ==
                data::packDataId(data::LegacyGroupId::Main, 0x01C0),
            "Trade token uses retail TAB_inpsa base");
        require(tradeui::TradeTokenAPriority == 274 &&
                tradeui::TradeTokenBPriority == 275,
            "Trade token priorities match DISPLAY_TradeBasePriority");
        require(tradeui::TradeTokenAX == 10 &&
                tradeui::TradeTokenRightEdge == 790 &&
                tradeui::TradeTokenY == 235,
            "Trade token alignment constants match UDTrade retail values");
    }

    void testLifecycleAndMeasuredWidth()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        tradeui::TokenPlayback tokens;
        tradeui::State state{};
        rules::GameState game{};
        state.playerA = 0;
        state.playerB = 1;
        game.players[0].token = 2;
        game.players[1].token = 5;

        require(tokens.sync(state, game, display::Screen2D::Main, playback).has_value() &&
                playback.commands().pendingCount() == 0,
            "off-view Trade tokens queue nothing");
        require(tokens.sync(state, game, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 4,
            "opening Trade queues Start+Move for both player tokens");
        require(playback.update(0).has_value() && playback.world2D().size() == 2,
            "Trade token opening publishes two 2D roots");

        const auto tokenA = tradeui::tradeTokenSequence(2);
        const auto tokenB = tradeui::tradeTokenSequence(5);
        const auto* a = object(playback, tokenA, tradeui::TradeTokenAPriority);
        const auto* b = object(playback, tokenB, tradeui::TradeTokenBPriority);
        require(a && a->worldTransform.values[6] == 10.0F &&
                a->worldTransform.values[7] == 235.0F,
            "player A token preserves retail StartXY(10,235)");
        require(b && b->worldTransform.values[6] == 788.0F &&
                b->worldTransform.values[7] == 235.0F,
            "player B x uses 790 minus measured 2-pixel bitmap width");

        require(tokens.sync(state, game, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 0,
            "unchanged Trade tokens queue no redundant commands");

        game.players[0].token = 3;
        require(tokens.sync(state, game, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 3,
            "changing player A token queues Stop+Start+Move only for A");
        require(playback.update(1).has_value() &&
                playback.runtime().matching(tokenA,
                    tradeui::TradeTokenAPriority).empty(),
            "old player A Trade token is removed");
        require(object(playback, tradeui::tradeTokenSequence(3),
                tradeui::TradeTokenAPriority) != nullptr,
            "new player A Trade token is published");

        state.playerB = rules::MaxPlayers;
        require(tokens.sync(state, game, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 1,
            "bank/sentinel player B removes only its token");
        require(playback.update(2).has_value() && playback.world2D().size() == 1,
            "bank/sentinel transition leaves only player A token");

        require(tokens.sync(state, game, display::Screen2D::Main, playback).has_value() &&
                playback.commands().pendingCount() == 1 &&
                playback.update(3).has_value() && playback.world2D().size() == 0,
            "leaving Trade stops the remaining player token");
    }

    void testTransactionalFailures()
    {
        SyntheticSequenceResources resources;
        tradeui::State state{};
        rules::GameState game{};
        state.playerA = 0;
        state.playerB = 1;
        game.players[0].token = 1;
        game.players[1].token = 4;

        engine::SequencePlayback invalid(resources.service.snapshot());
        tradeui::TokenPlayback invalidTokens;
        game.players[1].token = static_cast<std::uint8_t>(rules::MaxTokens);
        const auto invalidResult = invalidTokens.sync(
            state, game, display::Screen2D::Trade, invalid);
        require(!invalidResult && invalid.commands().pendingCount() == 0,
            "out-of-range Trade token rejects before queue mutation");

        game.players[1].token = 4;
        engine::SequencePlayback missing(nullptr);
        tradeui::TokenPlayback missingTokens;
        const auto missingResult = missingTokens.sync(
            state, game, display::Screen2D::Trade, missing);
        require(!missingResult && missing.commands().pendingCount() == 0,
            "missing Trade token resources reject before queue mutation");

        engine::SequencePlayback full(resources.service.snapshot());
        tradeui::TokenPlayback fullTokens;
        for (std::size_t count = 0;
             count < sequence::SequenceCommandQueue::Capacity - 3; ++count)
        {
            if (!full.commands().enqueue(
                    sequence::StopSequenceCommand{1, 0, false}))
                throw std::runtime_error("Trade token FIFO setup failed");
        }
        const auto before = full.commands().pendingCount();
        const auto noRoom = fullTokens.sync(
            state, game, display::Screen2D::Trade, full);
        require(!noRoom && full.commands().pendingCount() == before,
            "insufficient FIFO preserves hidden Trade token state transactionally");

        require(fullTokens.sync(state, game, display::Screen2D::Main, full).has_value() &&
                full.commands().pendingCount() == before,
            "failed Trade token open does not publish partial current state");
    }
}

int main()
{
    try
    {
        testRetailConstants();
        testLifecycleAndMeasuredWidth();
        testTransactionalFailures();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
