#include "DiceDisplay.hpp"
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

    void testIdsAnd2DPlan()
    {
        using namespace monopoly;
        const std::array<std::uint8_t, 2> ones{1, 1};
        const std::array<std::uint8_t, 2> sixes{6, 6};
        expect(data::dataTag(dice::roll3DSequence(ones)) == 0x0537 &&
            data::dataTag(dice::roll3DSequence(sixes)) == 0x057D,
            "3D roll IDs use mnd11a + (die1-1)*12 + (die2-1)*2");
        expect(data::dataTag(dice::idle3DSequence(ones)) == 0x057F &&
            data::dataTag(dice::idle3DSequence(sixes)) == 0x05C5,
            "3D idle IDs use mndi11a with the same 6x6 layout");
        expect(dice::roll3DSequence({0, 6}) == data::EmptyDataId &&
            dice::idle3DSequence({1, 7}) == data::EmptyDataId,
            "invalid dice values never index outside the historical tables");

        const auto fixed = dice::plan2D({4, 6}, false, true);
        expect(fixed.dice[0] && fixed.dice[1] && !fixed.bobbing &&
            data::dataTag(fixed.dice[0]->sequence) == 0x0099 &&
            data::dataTag(fixed.dice[1]->sequence) == 0x009B,
            "fixed IBar dice select tmpdc1..tmpdc6 by face");
        expect(fixed.dice[0]->priority == 256 && fixed.dice[1]->priority == 257 &&
            fixed.dice[0]->x == -35 && fixed.dice[1]->x == -11,
            "fixed dice preserve IBar priorities and -35/-11 positions");

        const auto bob = dice::plan2D({0, 0}, true, true);
        expect(bob.bobbing && bob.dice[0] && bob.dice[1] &&
            data::dataTag(bob.dice[0]->sequence) == 0x009C &&
            data::dataTag(bob.dice[1]->sequence) == 0x009C,
            "roll prompt uses the shared tmpdcbob sequence for both dice");
        expect(!bob.dice[0]->dropFrames && bob.dice[1]->dropFrames &&
            bob.dice[0]->loop && bob.dice[1]->loop,
            "second bobbing die uses DropDropFrames while both loop");
        const auto hidden = dice::plan2D({4, 6}, false, false);
        expect(!hidden.dice[0] && !hidden.dice[1],
            "hidden IBar suppresses fixed 2D dice");
    }

    void test3DRollTimingAndIdleReturn()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        rules::GameState state{};
        state.dice = {2, 4};
        dice::Playback dicePlayback;

        const auto idle = dicePlayback.tick(10, true, true, state, playback);
        expect(idle && idle->idleChanged &&
            dicePlayback.current3DSequence() == dice::idle3DSequence(state.dice),
            "valid board dice start the persistent 3D idle");
        expect(playback.update(10).has_value(),
            "initial 3D dice idle reaches SequenceRuntime");

        dice::RollRequest request{};
        request.values = state.dice;
        request.player = 0;
        request.lockTick = 20;
        expect(dicePlayback.begin(request).has_value(),
            "dice roll request arms playback");
        const auto armed = dicePlayback.tick(20, true, true, state, playback);
        expect(armed && armed->activeRoll && armed->cameraTakeover &&
            !armed->startedRoll && dicePlayback.current3DSequence() == data::EmptyDataId,
            "roll notification stops the old idle immediately and takes camera control");
        expect(playback.update(20).has_value(),
            "old idle stop drains before the delayed roll starts");

        const auto at35 = dicePlayback.tick(55, true, true, state, playback);
        expect(at35 && !at35->startedRoll,
            "elapsed 35 ticks does not yet start the 3D roll");
        expect(playback.update(55).has_value(), "tick 55 updates runtime cleanly");

        const auto at36 = dicePlayback.tick(56, true, true, state, playback);
        expect(at36 && at36->startedRoll &&
            dicePlayback.current3DSequence() == dice::roll3DSequence(state.dice),
            "elapsed 36 ticks starts mndXYa with drop-frames semantics");
        expect(playback.update(56).has_value() &&
            playback.runtime().info(dice::roll3DSequence(state.dice),
                dice::Generic3DPriority, false).has_value(),
            "3D roll sequence is live at generic priority 100");

        const auto at90 = dicePlayback.tick(110, true, true, state, playback);
        expect(at90 && !at90->cameraRelease,
            "elapsed 90 ticks keeps dice camera ownership");
        const auto at91 = dicePlayback.tick(111, true, true, state, playback);
        expect(at91 && at91->cameraRelease && at91->announceRoll,
            "elapsed 91 ticks releases camera control and announces the roll");

        const auto at120 = dicePlayback.tick(140, true, true, state, playback);
        expect(at120 && !at120->queueRelease && at120->activeRoll,
            "elapsed 120 ticks still holds the game queue lock");

        const auto at121 = dicePlayback.tick(141, true, true, state, playback);
        expect(at121 && at121->queueRelease && !at121->activeRoll &&
            at121->idleChanged &&
            dicePlayback.current3DSequence() == dice::idle3DSequence(state.dice),
            "elapsed 121 ticks releases the queue and restores idle dice in the same show cycle");
        expect(playback.update(141).has_value(),
            "roll-to-idle transition drains atomically");
        expect(!playback.runtime().info(dice::roll3DSequence(state.dice),
                dice::Generic3DPriority, false) &&
            playback.runtime().info(dice::idle3DSequence(state.dice),
                dice::Generic3DPriority, false),
            "roll sequence is gone and idle sequence is live after release");
    }

    void testIBarDisappearanceReleasesImmediately()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        rules::GameState state{};
        state.dice = {1, 1};
        dice::Playback dicePlayback;
        expect(dicePlayback.tick(1, true, true, state, playback).has_value(),
            "idle baseline queues before early-release test");
        expect(playback.update(1).has_value(), "idle baseline drains");

        dice::RollRequest request{};
        request.values = state.dice;
        request.lockTick = 200;
        expect(dicePlayback.begin(request).has_value(), "second roll begins");
        const auto hidden = dicePlayback.tick(200, false, false, state, playback);
        expect(hidden && hidden->queueRelease && !hidden->activeRoll,
            "hidden IBar releases the dice queue lock immediately");
        expect(dicePlayback.current3DSequence() == data::EmptyDataId,
            "hidden board leaves no 3D dice idle after early release");
    }
}

int main()
{
    testIdsAnd2DPlan();
    test3DRollTimingAndIdleReturn();
    testIBarDisappearanceReleasesImmediately();
    if (failures != 0)
        std::cerr << failures << " dice display failure(s)\n";
    return failures == 0 ? 0 : 1;
}
