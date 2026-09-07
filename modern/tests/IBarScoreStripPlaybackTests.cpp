#include "IBarScoreStripPlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;

    void require(bool condition, const char* message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) throw std::runtime_error(message);
    }

    [[nodiscard]] const engine::SequenceWorld2DObject* objectAtPriority(
        engine::SequencePlayback& playback,
        std::uint16_t priority)
    {
        for (const auto node : playback.world2D().order())
        {
            const auto* object = playback.world2D().find(node);
            if (object && object->priority == priority) return object;
        }
        return nullptr;
    }
    void testPlan()
    {
        rules::GameState state{};
        state.numberOfPlayers = 4;
        state.players[0].token = 2;
        state.players[0].colour = 3;
        state.players[0].cash = 1234;
        state.players[0].name = L"Alice";
        state.players[1].token = 5;
        state.players[1].colour = 1;
        state.players[1].cash = 987;
        state.players[1].name = L"Bob";
        state.players[1].currentSquare = 40;

        ibar::ScoreStripInputs inputs{};
        inputs.visiblePlayers[0] = true;
        inputs.visiblePlayers[1] = true;
        inputs.gameInProgress = true;
        inputs.hoveredPlayer = 1;

        const auto plan = ibar::planScoreStrip(state, inputs);
        require(plan.has_value(), "four-player score strip plan resolves");
        const auto& p0 = plan->players[0];
        const auto& p1 = plan->players[1];
        require(p0.visible && data::dataTag(p0.token) == 0x01C2 &&
                data::dataTag(p0.colourBar) == 0x01CE && p0.x == 5 &&
                p0.width == 184 && !p0.hovered && !p0.jailBars,
            "player 0 plan preserves token, large colour bar and x=5");
        require(p0.cash == 1234 && p0.name == L"Alice",
            "score plan preserves player cash/name as text data without rendering it");
        require(p1.visible && data::dataTag(p1.token) == 0x01C5 &&
                data::dataTag(p1.colourBar) == 0x01CC && p1.x == 192 &&
                p1.width == 184 && p1.hovered && p1.jailBars,
            "player 1 plan preserves hover and in-jail state");

        state.numberOfPlayers = 5;
        state.players[4].token = 1;
        state.players[4].colour = 5;
        ibar::ScoreStripInputs five{};
        five.visiblePlayers[4] = true;
        const auto small = ibar::planScoreStrip(state, five);
        require(small && small->players[4].x == 605 &&
                small->players[4].width == 127 &&
                data::dataTag(small->players[4].colourBar) == 0x01D6,
            "five-player plan switches to small colour atlas and x=605");

        state.players[4].token = rules::MaxTokens;
        require(!ibar::planScoreStrip(state, five),
            "token outside legacy 0..10 range is rejected");
        state.players[4].token = 1;
        state.players[4].colour = 6;
        require(!ibar::planScoreStrip(state, five),
            "colour outside legacy 0..5 range is rejected");
    }
    void testLifecycle()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::ScoreStripPlayback strip;
        rules::GameState state{};
        state.numberOfPlayers = 1;
        state.players[0].token = 2;
        state.players[0].colour = 3;
        state.players[0].cash = 1000;
        state.players[0].name = L"Player";

        ibar::ScoreStripInputs inputs{};
        inputs.visiblePlayers[0] = true;
        auto plan = ibar::planScoreStrip(state, inputs);
        require(plan && strip.sync(*plan, playback) &&
                playback.commands().pendingCount() == 5 && playback.update(0),
            "first score strip frame queues token/color StartXY plus jail Stop");
        require(playback.world2D().size() == 2 && strip.playerState(0).visible,
            "first score strip frame reaches two Overlay2D bitmap leaves");

        const auto* token = objectAtPriority(playback, ibar::ScoreBoxPriority);
        const auto* colour = objectAtPriority(playback, ibar::ScoreGeneralPriority + 1);
        require(token && token->worldTransform.values[6] == 290.0F &&
                token->worldTransform.values[7] == 565.0F,
            "token preserves StartXY(scoreX+5, ScoreY+5) at priority 305");
        require(colour && colour->worldTransform.values[6] == 285.0F &&
                colour->worldTransform.values[7] == 560.0F,
            "colour bar preserves StartXY(scoreX, ScoreY) at priority 257");
        require(strip.sync(*plan, playback) && playback.commands().pendingCount() == 0,
            "unchanged score strip queues no commands");

        inputs.hoveredPlayer = 0;
        plan = ibar::planScoreStrip(state, inputs);
        require(plan && strip.sync(*plan, playback) &&
                playback.commands().pendingCount() == 7 && playback.update(1),
            "player hover restarts token/color and reissues jail Stop like legacy corrective path");
        token = objectAtPriority(playback, ibar::ScoreBoxPriority);
        colour = objectAtPriority(playback, ibar::ScoreGeneralPriority + 1);
        require(token && colour && token->worldTransform.values[7] == 566.0F &&
                colour->worldTransform.values[7] == 561.0F,
            "hover lowers token and colour bar by one logical pixel");

        state.players[0].currentSquare = 40;
        inputs.gameInProgress = true;
        plan = ibar::planScoreStrip(state, inputs);
        require(plan && strip.sync(*plan, playback) &&
                playback.commands().pendingCount() == 9 && playback.update(2),
            "entering jail restarts score strip and starts jail bars transactionally");
        const auto* jail = objectAtPriority(playback, ibar::ScoreBoxPriority + 1);
        require(jail && jail->worldTransform.values[6] == 286.0F &&
                jail->worldTransform.values[7] == 560.0F,
            "hovered jail bars preserve StartXY(scoreX+1, ScoreY) at priority 306");

        state.players[0].cash = 1500;
        state.players[0].name = L"Renamed";
        const auto textOnly = ibar::planScoreStrip(state, inputs);
        require(textOnly && textOnly->players[0].cash == 1500 &&
                textOnly->players[0].name == L"Renamed" &&
                strip.sync(*textOnly, playback) &&
                playback.commands().pendingCount() == 0,
            "cash/name changes update text snapshot without inventing bitmap redraw commands");

        inputs.visiblePlayers[0] = false;
        const auto hidden = ibar::planScoreStrip(state, inputs);
        require(hidden && strip.sync(*hidden, playback) &&
                playback.commands().pendingCount() == 3 && playback.update(3),
            "hiding jailed player stops colour, token and jail bars in source order");
        require(!strip.playerState(0).visible && playback.world2D().size() == 0,
            "hidden player score strip removes all static Overlay2D leaves");
    }
    void testCashTimingSnapshot()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::ScoreStripPlayback strip;
        rules::GameState state{};
        state.numberOfPlayers = 1;
        state.players[0].token = 1;
        state.players[0].colour = 2;
        state.players[0].cash = 1000;
        state.players[0].name = L"Initial";
        ibar::ScoreStripInputs inputs{};
        inputs.visiblePlayers[0] = true;
        inputs.tick = 100;

        auto plan = ibar::planScoreStrip(state, inputs);
        require(plan && strip.sync(*plan, playback) && playback.update(100),
            "cash timing fixture starts visible score strip at tick 100");
        auto text = strip.textState(0);
        require(text.displayedCash == 1000 && text.lastCashUpdateTick == 100 &&
                text.lastCashChange == ibar::ScoreCashChange::Up && text.redrawRequested,
            "initial cash snapshot updates immediately and records future CashUp SFX direction");

        state.players[0].cash = 1500;
        inputs.tick = 119;
        plan = ibar::planScoreStrip(state, inputs);
        require(plan && strip.sync(*plan, playback) && playback.commands().pendingCount() == 0,
            "cash-only change never invents bitmap sequence commands");
        text = strip.textState(0);
        require(text.displayedCash == 1000 && text.lastCashChange == ibar::ScoreCashChange::None &&
                text.redrawRequested,
            "cash remains at previous displayed value before legacy 20-tick gate");

        inputs.tick = 120;
        plan = ibar::planScoreStrip(state, inputs);
        require(plan && strip.sync(*plan, playback),
            "cash snapshot accepts the exact twentieth tick");
        text = strip.textState(0);
        require(text.displayedCash == 1500 && text.lastCashUpdateTick == 120 &&
                text.lastCashChange == ibar::ScoreCashChange::Up,
            "cash updates at tick 120 and records CashUp direction");

        state.players[0].cash = 900;
        inputs.tick = 139;
        plan = ibar::planScoreStrip(state, inputs);
        require(plan && strip.sync(*plan, playback) &&
                strip.textState(0).displayedCash == 1500,
            "cash decrease remains gated through tick 139");

        inputs.tick = 140;
        plan = ibar::planScoreStrip(state, inputs);
        require(plan && strip.sync(*plan, playback),
            "cash decrease reaches its twentieth tick");
        text = strip.textState(0);
        require(text.displayedCash == 900 && text.lastCashUpdateTick == 140 &&
                text.lastCashChange == ibar::ScoreCashChange::Down,
            "cash decrease updates at tick 140 and records CashDown direction");

        state.players[0].name = L"Renamed";
        inputs.tick = 141;
        plan = ibar::planScoreStrip(state, inputs);
        require(plan && strip.sync(*plan, playback),
            "name-only score change is accepted without bitmap commands");
        text = strip.textState(0);
        require(text.printedName == L"Renamed" && text.displayedCash == 900 &&
                text.lastCashChange == ibar::ScoreCashChange::None && text.redrawRequested,
            "name snapshot updates independently of cash timing");
    }

    void testFailureIsTransactional()
    {
        SyntheticSequenceResources resources;
        rules::GameState state{};
        state.numberOfPlayers = 1;
        state.players[0].token = 1;
        state.players[0].colour = 2;
        ibar::ScoreStripInputs inputs{};
        inputs.visiblePlayers[0] = true;
        const auto plan = ibar::planScoreStrip(state, inputs);
        require(plan.has_value(), "transaction fixture score plan resolves");

        engine::SequencePlayback missing(nullptr);
        ibar::ScoreStripPlayback missingStrip;
        const auto unavailable = missingStrip.sync(*plan, missing);
        require(!unavailable && !missingStrip.playerState(0).visible &&
                missingStrip.textState(0).displayedCash == -1 &&
                missingStrip.textState(0).printedName.empty() &&
                missing.commands().pendingCount() == 0,
            "missing score resource queues no partial bitmap or text transition");

        engine::SequencePlayback full(resources.service.snapshot());
        ibar::ScoreStripPlayback fullStrip;
        for (std::size_t count = 0;
             count < sequence::SequenceCommandQueue::Capacity - 4;
             ++count)
        {
            if (!full.commands().enqueue(sequence::StopSequenceCommand{1, 0, false}))
                throw std::runtime_error("FIFO setup failed");
        }
        const auto noRoom = fullStrip.sync(*plan, full);
        require(!noRoom && !fullStrip.playerState(0).visible &&
                fullStrip.textState(0).displayedCash == -1 &&
                fullStrip.textState(0).printedName.empty() &&
                full.commands().pendingCount() ==
                    sequence::SequenceCommandQueue::Capacity - 4,
            "insufficient FIFO preserves complete bitmap and text score strip state");
    }
}

int main()
{
    try
    {
        testPlan();
        testLifecycle();
        testCashTimingSnapshot();
        testFailureIsTransactional();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
