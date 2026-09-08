#include "TradeBackdropPlayback.hpp"
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
        require(roots.size() == 1, "exact Trade backdrop DataID/priority has one root");
        return playback.world2D().find(roots.front());
    }

    void testRetailConstants()
    {
        require(tradeui::tradeBackdropSequence(tradeui::TradeBackgroundTag) ==
                data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x02CD),
            "Trade backdrop uses retail CNK_tradebg");
        require(tradeui::TradeBackdropPriority == 80 &&
                tradeui::TradeColourRailPriority == 95 &&
                tradeui::TradeDisplayBoardPriority == 1000,
            "Trade backdrop low/high priorities match display.h retail constants");
        require(tradeui::TradeTurntablePriority == 130 &&
                tradeui::TradeArrowPriority == 140 &&
                tradeui::TradeDisplayPanelPriority == 145,
            "Trade turntable/arrow/display priorities match retail values");
        require(tradeui::TradeFutureButtonPriority == 150 &&
                tradeui::TradeCreateButtonPriority == 155 &&
                tradeui::TradeImmunityButtonPriority == 160,
            "Trade Future/Create/Immunity priorities match retail values");
        require(tradeui::TradeLoopToBeginning == 3,
            "Trade animated buttons use legacy LoopToBeginning action");
    }

    void testFullBackdropLifecycle()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        tradeui::BackdropPlayback backdrop;
        tradeui::State state{};
        rules::GameState game{};
        state.playerA = 0;
        state.playerB = 1;
        game.players[0].colour = 2;
        game.players[1].colour = 5;
        game.options.futureRentTradingAllowed = true;
        game.options.immunitiesTradingAllowed = true;

        require(backdrop.sync(state, game, display::Screen2D::Main, playback).has_value() &&
                playback.commands().pendingCount() == 0 && !backdrop.visible(),
            "off-view Trade backdrop queues nothing");
        require(backdrop.sync(state, game, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 25,
            "full Trade backdrop opening queues 25 commands atomically");
        require(playback.update(0).has_value() && playback.world2D().size() == 11 &&
                backdrop.activeCount() == 11,
            "full Trade backdrop publishes eleven autonomous roots");

        const auto* background = object(playback,
            tradeui::tradeBackdropSequence(tradeui::TradeBackgroundTag),
            tradeui::TradeBackdropPriority);
        require(background && background->worldTransform.values[6] == 0.0F &&
                background->worldTransform.values[7] == 0.0F,
            "Trade background is anchored at retail origin");
        const auto* turntable = object(playback,
            tradeui::tradeBackdropSequence(tradeui::TradeTurntableTag),
            tradeui::TradeTurntablePriority);
        require(turntable && turntable->worldTransform.values[6] == 1.0F &&
                turntable->worldTransform.values[7] == 0.0F,
            "Trade turntable preserves retail StartXY(1,0)");

        const auto leftRailId = tradeui::tradeBackdropSequence(
            static_cast<data::DataTag>(tradeui::TradeLeftColourBaseTag + 2u));
        const auto rightRailId = tradeui::tradeBackdropSequence(
            static_cast<data::DataTag>(tradeui::TradeRightColourBaseTag + 5u));
        require(object(playback, leftRailId, tradeui::TradeColourRailPriority) != nullptr &&
                object(playback, rightRailId, tradeui::TradeColourRailPriority) != nullptr,
            "Trade colour rails select exact player colour CNKs");

        require(playback.update(5).has_value() &&
                playback.runtime().matching(
                    tradeui::tradeBackdropSequence(tradeui::TradeCreateButtonTag),
                    tradeui::TradeCreateButtonPriority).size() == 1 &&
                playback.runtime().matching(
                    tradeui::tradeBackdropSequence(tradeui::TradeFutureButtonTag),
                    tradeui::TradeFutureButtonPriority).size() == 1 &&
                playback.runtime().matching(
                    tradeui::tradeBackdropSequence(tradeui::TradeImmunityButtonTag),
                    tradeui::TradeImmunityButtonPriority).size() == 1,
            "Trade animated buttons survive natural end via LoopToBeginning");
        require(backdrop.sync(state, game, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 0,
            "unchanged Trade backdrop queues no redundant commands");

        game.players[0].colour = 3;
        require(backdrop.sync(state, game, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 3,
            "changing player A colour replaces only its rail with Stop+Start+Move");
        require(playback.update(6).has_value() &&
                playback.runtime().matching(leftRailId,
                    tradeui::TradeColourRailPriority).empty(),
            "old player A colour rail is removed");
        const auto newLeftRailId = tradeui::tradeBackdropSequence(
            static_cast<data::DataTag>(tradeui::TradeLeftColourBaseTag + 3u));
        require(object(playback, newLeftRailId,
                tradeui::TradeColourRailPriority) != nullptr,
            "new player A colour rail is published");

        game.options.futureRentTradingAllowed = false;
        game.options.immunitiesTradingAllowed = false;
        require(backdrop.sync(state, game, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 3,
            "disabling both contract options stops only three animated Trade buttons");
        require(playback.update(7).has_value() && backdrop.activeCount() == 8 &&
                playback.world2D().size() == 8,
            "contract-option removal leaves six base roots plus two colour rails");

        state.playerA = rules::MaxPlayers;
        require(backdrop.sync(state, game, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 3,
            "bank/sentinel player A swaps to special left colour rail");
        require(playback.update(8).has_value(),
            "special left colour rail transition executes");
        const auto specialLeft = tradeui::tradeBackdropSequence(
            static_cast<data::DataTag>(tradeui::TradeLeftColourBaseTag + 6u));
        const auto* special = object(playback, specialLeft,
            tradeui::TradeColourRailPriority);
        require(special && special->worldTransform.values[6] == 1.0F,
            "special left colour rail preserves retail x=1 offset");

        require(backdrop.sync(state, game, display::Screen2D::Main, playback).has_value() &&
                playback.commands().pendingCount() == 8,
            "leaving Trade queues one Stop for every remaining backdrop root");
        require(playback.update(9).has_value() && playback.world2D().size() == 0 &&
                !backdrop.visible(),
            "leaving Trade removes complete autonomous backdrop");
    }

    void testTransactionalFailures()
    {
        tradeui::State state{};
        state.playerA = 0;
        state.playerB = 1;
        rules::GameState game{};
        game.players[0].colour = 1;
        game.players[1].colour = 4;
        game.options.futureRentTradingAllowed = true;
        game.options.immunitiesTradingAllowed = true;

        engine::SequencePlayback offViewMissing(nullptr);
        tradeui::BackdropPlayback offViewBackdrop;
        require(offViewBackdrop.sync(state, game, display::Screen2D::Main,
                    offViewMissing).has_value() &&
                offViewMissing.commands().pendingCount() == 0,
            "off-view backdrop does not require resources");

        engine::SequencePlayback missing(nullptr);
        tradeui::BackdropPlayback missingBackdrop;
        const auto missingResult = missingBackdrop.sync(
            state, game, display::Screen2D::Trade, missing);
        require(!missingResult && missing.commands().pendingCount() == 0 &&
                !missingBackdrop.visible(),
            "missing backdrop resource rejects transition before queueing");
        SyntheticSequenceResources resources;
        engine::SequencePlayback full(resources.service.snapshot());
        tradeui::BackdropPlayback fullBackdrop;
        bool filled = true;
        for (std::size_t i = 0;
             i < sequence::SequenceCommandQueue::Capacity - 24; ++i)
        {
            const auto queued = full.commands().enqueue(
                sequence::StopSequenceCommand{data::EmptyDataId, 0, false});
            filled = filled && queued.has_value();
        }
        require(filled,
            "FIFO fixture leaves only 24 slots for 25-command full backdrop open");
        const auto before = full.commands().pendingCount();
        const auto noRoom = fullBackdrop.sync(
            state, game, display::Screen2D::Trade, full);
        require(!noRoom && full.commands().pendingCount() == before &&
                !fullBackdrop.visible(),
            "insufficient FIFO preserves hidden backdrop state transactionally");

        game.players[0].colour = 6;
        engine::SequencePlayback invalid(resources.service.snapshot());
        tradeui::BackdropPlayback invalidBackdrop;
        const auto invalidResult = invalidBackdrop.sync(
            state, game, display::Screen2D::Trade, invalid);
        require(!invalidResult && invalid.commands().pendingCount() == 0 &&
                !invalidBackdrop.visible(),
            "out-of-range player colour is rejected before queue mutation");
    }
}

int main()
{
    try
    {
        testRetailConstants();
        testFullBackdropLifecycle();
        testTransactionalFailures();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
