#include "PieceMovePlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace
{
    using namespace monopoly;
    using namespace monopoly::pieces;
    int failures{};

    void expect(bool value, std::string_view text)
    {
        std::cout << (value ? "[PASS] " : "[FAIL] ") << text << '\n';
        if (!value) ++failures;
    }

    constexpr std::uint16_t MovePriority = 100;

    data::DataId finiteSequenceId() noexcept
    {
        return data::packDataId(data::LegacyGroupId::Main, 1);
    }

    PieceMoveInstruction moveItem(BoardCameraView camera,
        std::int32_t start, std::int32_t landing)
    {
        return {false, 0, camera, finiteSequenceId(), start, landing, false};
    }

    PieceMoveInstruction cameraItem(BoardCameraView camera, std::int32_t square)
    {
        return {true, 0, camera, data::EmptyDataId, square, square, false};
    }

    void testAtomicLegacyStartContract()
    {
        SyntheticSequenceResources fixture;
        engine::SequencePlayback playback(fixture.service.snapshot());
        const auto id = finiteSequenceId();

        const auto queued = playback.transitionRySTxzDropStayAtEnd(
            std::nullopt, id, MovePriority, 0.25F, 1.0F, 10.0F, 20.0F);
        expect(queued && playback.commands().pendingCount() == 3,
            "StartRySTxzDrop queues Start, MoveRySTxz and StayAtEnd atomically");
        expect(playback.update(0).has_value(), "finite sequence starts at parent tick zero");
        auto info = playback.runtime().info(id, MovePriority, false);
        expect(info && info->sequenceClock == 0 && info->endTime == 100 &&
            info->sequenceToWorldTransformation &&
            std::fabs(info->sequenceToWorldTransformation->values[12] - 10.0F) < 0.001F &&
            std::fabs(info->sequenceToWorldTransformation->values[14] - 20.0F) < 0.001F,
            "atomic start publishes exact finite duration and RySTxz translation");

        expect(playback.update(10).has_value(), "finite sequence advances to tick ten");
        info = playback.runtime().info(id, MovePriority, false);
        expect(info && info->sequenceClock == 10,
            "DropDropFrames override catches sequence clock up to parent clock");

        expect(playback.update(100).has_value(), "finite sequence reaches its end tick");
        info = playback.runtime().info(id, MovePriority, false);
        expect(info && info->sequenceClock == 100,
            "StayAtEnd override keeps disk-Stop sequence alive at its last frame");

        const auto replaced = playback.transitionRySTxzDropStayAtEnd(
            id, id, MovePriority, -0.5F, 1.0F, 30.0F, 40.0F);
        expect(replaced && playback.commands().pendingCount() == 4,
            "same-ID transition preflights Stop plus three startup commands");
        expect(playback.update(100).has_value(),
            "same-ID transition drains stop before replacement start");
        info = playback.runtime().info(id, MovePriority, false);
        expect(info && info->sequenceClock == 0 &&
            info->sequenceToWorldTransformation &&
            std::fabs(info->sequenceToWorldTransformation->values[12] - 30.0F) < 0.001F &&
            std::fabs(info->sequenceToWorldTransformation->values[14] - 40.0F) < 0.001F,
            "replacement sequence starts fresh at the new RySTxz position");
    }

    void testNormalStackEscalation()
    {
        SyntheticSequenceResources fixture;
        engine::SequencePlayback playback(fixture.service.snapshot());
        PieceMovePlayback controller;
        PieceMovePlan plan{};
        plan.sourceSquare = 1;
        plan.destinationSquare = 6;
        plan.instructions.push_back(moveItem(BoardCameraView::FifteenTiles01, 1, 6));
        plan.instructions.push_back(cameraItem(BoardCameraView::ThreeTiles02, 6));
        expect(controller.begin(std::move(plan)).has_value(),
            "normal TokenAnimStack begins without executing before its first tick");

        auto step = controller.tick(true, playback);
        expect(step && step->active && step->startedSequence == finiteSequenceId() &&
            step->camera == BoardCameraView::FifteenTiles01 &&
            controller.stackIndex() == 1,
            "first stack tick starts sequence and publishes its preset camera");
        expect(playback.update(0).has_value(), "first stack sequence enters runtime");

        step = controller.tick(true, playback);
        expect(step && step->active && !step->camera && !step->startedSequence,
            "running sequence blocks stack escalation before endTime");
        expect(playback.update(99).has_value(), "running sequence advances close to end");
        step = controller.tick(true, playback);
        expect(step && step->active && !step->completed,
            "sequence still blocks escalation one tick before its end");

        expect(playback.update(103).has_value(), "running sequence reaches held end frame at its four-tick cadence");
        step = controller.tick(true, playback);
        expect(step && step->completed && !step->active &&
            step->camera == BoardCameraView::ThreeTiles02 && step->stoppedSequence,
            "held sequence escalates to final camera-only item then terminates stack");
        expect(playback.commands().pendingCount() == 1,
            "stack termination queues the historical Stop command");
        expect(playback.update(103).has_value() &&
            !playback.runtime().info(finiteSequenceId(), MovePriority, false),
            "same-tick command processing removes the final held sequence");
    }

    void testBoardVisibilityAbort()
    {
        SyntheticSequenceResources fixture;
        engine::SequencePlayback playback(fixture.service.snapshot());
        PieceMovePlayback controller;
        PieceMovePlan plan{};
        plan.instructions.push_back(moveItem(BoardCameraView::FifteenTiles01, 1, 6));
        plan.instructions.push_back(cameraItem(BoardCameraView::ThreeTiles02, 6));
        expect(controller.begin(std::move(plan)).has_value(), "abort fixture begins");

        auto step = controller.tick(true, playback);
        expect(step && step->startedSequence && playback.update(0).has_value(),
            "abort fixture starts its first sequence");
        step = controller.tick(false, playback);
        expect(step && step->completed && step->stoppedSequence &&
            !controller.active(),
            "hidden board closes TokenAnimStack immediately like DISPLAY_IsBoardVisible");
        expect(playback.update(0).has_value() &&
            !playback.runtime().info(finiteSequenceId(), MovePriority, false),
            "hidden-board abort removes current token sequence on next queue drain");
    }

    void testVictoryLoop()
    {
        SyntheticSequenceResources fixture;
        engine::SequencePlayback playback(fixture.service.snapshot());
        PieceMovePlayback controller;
        PieceMovePlan plan{};
        plan.special = PieceMoveSpecial::OffBoardVictory;
        plan.instructions.push_back(moveItem(BoardCameraView::FifteenTiles01, 1, 2));
        plan.instructions.push_back(moveItem(BoardCameraView::FifteenTiles02, 2, 3));
        plan.loopBegin = 0;
        plan.loopEnd = 2;
        expect(controller.begin(std::move(plan)).has_value(), "victory loop fixture begins");

        auto step = controller.tick(true, playback);
        expect(step && step->startedSequence && playback.update(0).has_value(),
            "victory loop starts first sequence");
        expect(playback.update(100).has_value(), "first victory segment reaches end");
        step = controller.tick(true, playback);
        expect(step && step->looped && step->active &&
            step->camera == BoardCameraView::FifteenTiles02 &&
            controller.stackIndex() == 0,
            "TokenLoopAnimEnd wraps stack index back to TokenLoopAnimTop");

        expect(playback.update(100).has_value(),
            "replacement victory segment starts at same parent tick");
        expect(playback.update(200).has_value(), "second victory segment reaches end");
        step = controller.tick(true, playback);
        expect(step && step->active && step->startedSequence == finiteSequenceId() &&
            controller.stackIndex() == 1,
            "victory loop starts first stack item again after wrap");

        expect(playback.update(200).has_value(), "looped first segment enters runtime");
        step = controller.tick(false, playback);
        expect(step && step->completed && !controller.active(),
            "board disappearance terminates even an otherwise infinite victory loop");
        expect(playback.update(200).has_value(), "victory abort stop drains cleanly");
    }

    void testPlanValidation()
    {
        PieceMovePlayback controller;
        PieceMovePlan empty{};
        expect(!controller.begin(std::move(empty)),
            "empty special plan is not mistaken for a TokenAnimStack executor job");

        PieceMovePlan badVictory{};
        badVictory.special = PieceMoveSpecial::OffBoardVictory;
        badVictory.instructions.push_back(cameraItem(BoardCameraView::CornerGo, 0));
        badVictory.loopBegin = 1;
        badVictory.loopEnd = 1;
        expect(!controller.begin(std::move(badVictory)),
            "invalid victory loop bounds are rejected before runtime mutation");
    }
}

int main()
{
    testAtomicLegacyStartContract();
    testNormalStackEscalation();
    testBoardVisibilityAbort();
    testVictoryLoop();
    testPlanValidation();
    std::cout << (failures ? "Piece move-playback tests FAILED\n" :
        "Piece move-playback tests passed\n");
    return failures ? 1 : 0;
}
