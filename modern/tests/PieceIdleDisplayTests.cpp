#include "PieceIdleDisplay.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <string_view>

namespace
{
    int failures{};

    void expect(bool condition, std::string_view message)
    {
        if (condition) std::cout << "[PASS] " << message << '\n';
        else { std::cerr << "[FAIL] " << message << '\n'; ++failures; }
    }

    monopoly::rules::GameState twoPlayersOnGo()
    {
        monopoly::rules::GameState state{};
        state.numberOfPlayers = 2;
        state.players[0].token = 0;
        state.players[1].token = 1;
        state.players[0].currentSquare = 0;
        state.players[1].currentSquare = 0;
        return state;
    }

    monopoly::data::DataId idleSequence(std::uint8_t token)
    {
        using namespace monopoly::data;
        return packDataId(LegacyGroupId::ThreeD,
            static_cast<DataTag>(0x010D + 0x63 * token));
    }
    void testPersistentIdleLifecycle()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        pieces::PieceIdleState idleState;
        auto state = twoPlayersOnGo();
        expect(idleState.initializeNewGame(state).has_value(),
            "new-game idle occupancy initializes before persistent display");

        pieces::PieceIdleDisplay display;
        pieces::PieceIdleDisplayContext context{};
        context.boardVisible = true;
        context.animationsEnabled = true;

        const auto first = display.sync(state, idleState, context, playback);
        expect(first && first->started == 2 && first->moved == 0 && first->stopped == 0,
            "visible board starts one persistent idle per active player");
        expect(playback.update(0).has_value(),
            "persistent idle start commands execute in the sequencer");
        expect(playback.runtime().info(idleSequence(0), 224, false).has_value() &&
            playback.runtime().info(idleSequence(1), 225, false).has_value(),
            "persistent token idles use knamx0+token*99 and priority 224+player");

        const auto unchanged = display.sync(state, idleState, context, playback);
        expect(unchanged && unchanged->started == 0 && unchanged->moved == 0 &&
            unchanged->stopped == 0,
            "unchanged persistent idles do not restart or enqueue redundant moves");
    }
    void testPoseMoveAndVisibility()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        pieces::PieceIdleState idleState;
        auto state = twoPlayersOnGo();
        expect(idleState.initializeNewGame(state).has_value(),
            "idle occupancy initializes for pose-move test");

        pieces::PieceIdleDisplay display;
        pieces::PieceIdleDisplayContext context{};
        context.boardVisible = true;
        expect(display.sync(state, idleState, context, playback).has_value(),
            "persistent idle baseline queues successfully");
        expect(playback.update(0).has_value(),
            "persistent idle baseline reaches runtime");

        const auto planned = idleState.planTurnChange(state, 0);
        expect(planned.has_value(),
            "turn-change planner promotes player zero to center");
        const auto moved = display.sync(state, idleState, context, playback);
        expect(moved && moved->moved == 1 && moved->started == 0 && moved->stopped == 0,
            "same idle sequence moves in-place when only center/rest pose changes");
        expect(display.shownSequence(0) == idleSequence(0),
            "in-place pose update keeps the original persistent sequence DataID");

        context.idleMovingIn = static_cast<rules::PlayerNumber>(0);
        const auto hiddenOne = display.sync(state, idleState, context, playback);
        expect(hiddenOne && hiddenOne->stopped == 1 &&
            display.shownSequence(0) == data::EmptyDataId &&
            display.shownSequence(1) == idleSequence(1),
            "center/rest transition hides only the token currently moving in");
        expect(playback.update(1).has_value(),
            "individual idle stop reaches the runtime");
        expect(!playback.runtime().info(idleSequence(0), 224, false) &&
            playback.runtime().info(idleSequence(1), 225, false),
            "suppressed player is gone while the other idle remains live");

        context.idleMovingIn.reset();
        const auto restored = display.sync(state, idleState, context, playback);
        expect(restored && restored->started == 1,
            "persistent idle restarts after transition suppression ends");
        expect(playback.update(2).has_value(),
            "restored persistent idle reaches runtime");

        context.boardVisible = false;
        const auto hiddenAll = display.sync(state, idleState, context, playback);
        expect(hiddenAll && hiddenAll->stopped == 2,
            "hidden board stops every persistent token idle");
        expect(playback.update(3).has_value(),
            "board-hidden stops are drained by the sequencer");
        expect(!playback.runtime().info(idleSequence(0), 224, false) &&
            !playback.runtime().info(idleSequence(1), 225, false),
            "no persistent token remains in runtime when board is hidden");
    }

    void testProjectionMismatchIsRejected()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        pieces::PieceIdleState idleState;
        auto state = twoPlayersOnGo();
        expect(idleState.initializeNewGame(state).has_value(),
            "idle occupancy initializes before mismatch test");
        state.players[0].currentSquare = 1;

        pieces::PieceIdleDisplay display;
        pieces::PieceIdleDisplayContext context{};
        context.boardVisible = true;
        const auto result = display.sync(state, idleState, context, playback);
        expect(!result,
            "persistent idle rejects a RULE square that has no matching resting occupancy");
    }
}

int main()
{
    testPersistentIdleLifecycle();
    testPoseMoveAndVisibility();
    testProjectionMismatchIsRejected();

    if (failures != 0)
        std::cerr << failures << " persistent idle display failure(s)\n";
    return failures == 0 ? 0 : 1;
}
