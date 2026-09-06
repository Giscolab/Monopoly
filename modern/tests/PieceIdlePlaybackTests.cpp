#include "PieceIdlePlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <string_view>

namespace
{
    using namespace monopoly;
    int failures{};

    void expect(bool value, std::string_view text)
    {
        std::cout << (value ? "[PASS] " : "[FAIL] ") << text << '\n';
        if (!value) ++failures;
    }

    rules::GameState stateForTransition()
    {
        rules::GameState state{};
        state.numberOfPlayers = 3;
        for (rules::PlayerNumber player = 0; player < 3; ++player)
        {
            state.players[player].currentSquare = 0;
            state.players[player].token = player;
        }
        return state;
    }

    bool update(engine::SequencePlayback& playback, std::int32_t tick)
    { return playback.update(tick).has_value(); }

    void testTwoAnimationsRunAndFinishIndependently()
    {
        SyntheticSequenceResources fixture;
        engine::SequencePlayback playback(fixture.service.snapshot());
        auto state = stateForTransition();
        pieces::PieceIdleState idle;
        expect(idle.initialize(state, rules::PlayerNumber{1}).has_value(),
            "idle occupancy initializes for playback fixture");
        auto plan = idle.planTurnChange(state, 2);
        expect(plan && plan->movingOut && plan->movingIn,
            "playback fixture contains simultaneous out/in animations");

        pieces::PieceIdlePlayback machine;
        const auto outId = plan->movingOut->sequence;
        const auto inId = plan->movingIn->sequence;
        expect(machine.begin(std::move(*plan)).has_value(),
            "idle playback accepts a valid transition plan");
        auto step = machine.tick(playback);
        expect(step && step->active && !step->completed,
            "first idle playback tick queues both transition sequences");
        expect(update(playback, 0), "both idle transition starts enter runtime");
        expect(playback.runtime().info(outId, pieces::TokenPriority, false).has_value() &&
            playback.runtime().info(inId, pieces::TokenPriority, false).has_value(),
            "outgoing and incoming idle sequences share priority 224 and coexist");

        expect(playback.stop(outId, pieces::TokenPriority).has_value(),
            "test can remove outgoing sequence before incoming completes");
        expect(update(playback, 50), "outgoing stop drains while incoming keeps running");
        step = machine.tick(playback);
        expect(step && step->active && !step->completed &&
            machine.movingOutSequence() == data::EmptyDataId &&
            machine.movingInSequence() == inId,
            "missing outgoing GetInfo is treated complete without ending incoming branch");
        expect(update(playback, 50), "redundant historical Stop for absent outgoing drains");

        expect(update(playback, 100), "incoming idle sequence reaches held final frame");
        step = machine.tick(playback);
        expect(step && step->completed && !machine.active(),
            "idle playback completes only when both transition branches are done");
        expect(update(playback, 100), "final incoming Stop drains from sequence FIFO");
        expect(!playback.runtime().info(inId, pieces::TokenPriority, false),
            "completed idle playback leaves no transition sequence in runtime");
    }

    void testNewGameCanMoveFirstPlayerInWithoutOutgoingBranch()
    {
        SyntheticSequenceResources fixture;
        engine::SequencePlayback playback(fixture.service.snapshot());
        auto state = stateForTransition();
        pieces::PieceIdleState idle;
        expect(idle.initializeNewGame(state).has_value(),
            "new-game reverse GO occupancy initializes for playback");
        auto plan = idle.planTurnChange(state, 0);
        expect(plan && !plan->movingOut && plan->movingIn &&
            plan->movingIn->restingSlot == 2,
            "first turn has only rest-to-center animation from reversed GO slot");

        pieces::PieceIdlePlayback machine;
        expect(machine.begin(std::move(*plan)).has_value(),
            "single-branch first-turn idle plan is accepted");
        expect(machine.tick(playback).has_value() && update(playback, 0),
            "single incoming branch starts normally");
        expect(update(playback, 100), "single incoming branch reaches its end");
        const auto done = machine.tick(playback);
        expect(done && done->completed,
            "first-turn playback completes without requiring an outgoing sequence");
    }
}

int main()
{
    testTwoAnimationsRunAndFinishIndependently();
    testNewGameCanMoveFirstPlayerInWithoutOutgoingBranch();
    if (failures != 0) std::cerr << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
