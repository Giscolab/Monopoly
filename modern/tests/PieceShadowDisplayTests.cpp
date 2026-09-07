#include "PieceShadowDisplay.hpp"
#include "SequenceTransforms.hpp"
#include "SyntheticSequenceResources.hpp"

#include <cmath>
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

    bool near(float left, float right) noexcept
    {
        return std::fabs(left - right) < 0.001F;
    }

    monopoly::data::DataId threeD(monopoly::data::DataTag tag)
    {
        return monopoly::data::packDataId(
            monopoly::data::LegacyGroupId::ThreeD, tag);
    }

    monopoly::sequence::Matrix3D poseMatrix(
        float yaw, float x, float y, float z)
    {
        auto matrix = monopoly::sequence::moveRySTxzTransform(yaw, 1.0F, x, z);
        matrix.values[13] = y;
        return matrix;
    }

    void testRuntimePoseAndMove()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        pieces::PieceShadowDisplay display;
        rules::GameState state{};
        state.numberOfPlayers = 1;
        state.currentPlayer = 0;
        state.players[0].token = 2;
        state.players[0].currentSquare = 5;

        const auto moving = threeD(0x0537);
        expect(playback.startMoved(moving, pieces::Generic3DPriority,
            poseMatrix(0.5F, 13.0F, 7.0F, 21.0F)).has_value() &&
            playback.update(0).has_value(),
            "moving token fixture publishes an actual runtime pose");

        pieces::PieceShadowDisplayContext context{};
        context.boardVisible = true;
        context.lightingOn = true;
        context.game3DOn = true;
        context.currentUIPlayer = 0;
        context.currentPlayerTokenSequenceActive = true;
        context.runtimeSources.movingSequence = moving;

        const auto started = display.sync(state, context, playback);
        expect(started && started->started == 1 && started->stopped == 0,
            "visible token starts one persistent shadow sequence");
        expect(playback.update(1).has_value(),
            "shadow start reaches the runtime");

        const auto shadow = threeD(
            static_cast<data::DataTag>(pieces::ShadowSequenceBaseTag + 2));
        const auto info = playback.runtime().info(
            shadow, pieces::ShadowPriorityBase, false);
        expect(info && info->sequenceToWorldTransformation &&
            near(info->sequenceToWorldTransformation->values[12], 13.0F) &&
            near(info->sequenceToWorldTransformation->values[13], pieces::ShadowGroundY) &&
            near(info->sequenceToWorldTransformation->values[14], 21.0F),
            "shadow follows token X/Z while legacy ground height is forced to Y=1");

        expect(playback.move(moving, pieces::Generic3DPriority,
            poseMatrix(0.75F, 23.0F, 9.0F, 31.0F)).has_value() &&
            playback.update(2).has_value(),
            "moving token fixture advances to a second runtime pose");
        const auto moved = display.sync(state, context, playback);
        expect(moved && moved->moved == 1 && moved->started == 0,
            "existing shadow moves instead of restarting");
        expect(playback.update(3).has_value(),
            "shadow move reaches the runtime");
        const auto movedInfo = playback.runtime().info(
            shadow, pieces::ShadowPriorityBase, false);
        expect(movedInfo && movedInfo->sequenceToWorldTransformation &&
            near(movedInfo->sequenceToWorldTransformation->values[12], 23.0F) &&
            near(movedInfo->sequenceToWorldTransformation->values[13], pieces::ShadowGroundY) &&
            near(movedInfo->sequenceToWorldTransformation->values[14], 31.0F),
            "shadow tracks the updated token runtime pose");

        const auto unchanged = display.sync(state, context, playback);
        expect(unchanged && unchanged->started == 0 && unchanged->stopped == 0 &&
            unchanged->moved == 0,
            "unchanged shadow state queues no redundant commands");
    }

    void testVisibilityGates()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        pieces::PieceShadowDisplay display;
        rules::GameState state{};
        state.numberOfPlayers = 1;
        state.currentPlayer = 0;
        state.players[0].token = 0;
        state.players[0].currentSquare = 10;

        const auto moving = threeD(0x0537);
        expect(playback.startMoved(moving, pieces::Generic3DPriority,
            poseMatrix(0.0F, 10.0F, 0.0F, 20.0F)).has_value() &&
            playback.update(0).has_value(),
            "visibility fixture publishes token runtime pose");

        pieces::PieceShadowDisplayContext context{};
        context.boardVisible = true;
        context.lightingOn = true;
        context.game3DOn = true;
        context.currentUIPlayer = 0;
        context.currentPlayerTokenSequenceActive = true;
        context.runtimeSources.movingSequence = moving;

        expect(display.sync(state, context, playback).has_value() &&
            playback.update(1).has_value(),
            "baseline shadow starts before visibility gates");

        context.lightingOn = false;
        const auto lightingOff = display.sync(state, context, playback);
        expect(lightingOff && lightingOff->stopped == 1,
            "lighting option off stops the shadow");
        expect(playback.update(2).has_value(),
            "lighting-off stop reaches runtime");

        context.lightingOn = true;
        context.paddywagonPlayer = static_cast<rules::PlayerNumber>(0);
        context.goingToJailStatus = 8;
        const auto wagonHidden = display.sync(state, context, playback);
        expect(wagonHidden && wagonHidden->started == 0,
            "paddywagon state 8 keeps the passenger shadow hidden");

        context.goingToJailStatus = 7;
        const auto wagonVisible = display.sync(state, context, playback);
        expect(wagonVisible && wagonVisible->started == 1,
            "paddywagon states other than 8 preserve the token shadow");
        expect(playback.update(3).has_value(),
            "paddywagon-visible shadow reaches runtime");

        context.boardVisible = false;
        const auto boardHidden = display.sync(state, context, playback);
        expect(boardHidden && boardHidden->stopped == 1,
            "hidden board stops the token shadow");
        expect(playback.update(4).has_value(),
            "board-hidden stop reaches runtime");

        context.boardVisible = true;
        context.game3DOn = false;
        const auto no3D = display.sync(state, context, playback);
        expect(no3D && no3D->started == 0,
            "2D-only game path does not start a 3D shadow");
    }

    void testBankruptAndInvalidToken()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        pieces::PieceShadowDisplay display;
        rules::GameState state{};
        state.numberOfPlayers = 1;
        state.currentPlayer = 0;
        state.players[0].token = 1;
        state.players[0].currentSquare = 41;

        const auto moving = threeD(0x0537);
        expect(playback.startMoved(moving, pieces::Generic3DPriority,
            poseMatrix(0.0F, 3.0F, 0.0F, 4.0F)).has_value() &&
            playback.update(0).has_value(),
            "bankrupt fixture publishes custom token sequence pose");

        pieces::PieceShadowDisplayContext context{};
        context.boardVisible = true;
        context.lightingOn = true;
        context.game3DOn = true;
        context.currentUIPlayer = 0;
        context.runtimeSources.movingSequence = moving;

        const auto hidden = display.sync(state, context, playback);
        expect(hidden && hidden->started == 0,
            "off-board player has no shadow after its token sequence ends");

        context.currentPlayerTokenSequenceActive = true;
        const auto animated = display.sync(state, context, playback);
        expect(animated && animated->started == 1,
            "off-board current player keeps shadow during custom token animation");
        expect(playback.update(1).has_value(),
            "off-board animated shadow reaches runtime");

        context.currentPlayerTokenSequenceActive = false;
        expect(display.sync(state, context, playback).has_value() &&
            playback.update(2).has_value(),
            "off-board shadow is removed after custom token animation");

        state.players[0].currentSquare = 10;
        state.players[0].token = rules::MaxTokens;
        const auto pendingBefore = playback.commands().pendingCount();
        const auto invalid = display.sync(state, context, playback);
        expect(!invalid && playback.commands().pendingCount() == pendingBefore &&
            display.shownSequence(0) == data::EmptyDataId,
            "invalid token is rejected transactionally without publishing a shadow");
    }
}

int main()
{
    testRuntimePoseAndMove();
    testVisibilityGates();
    testBankruptAndInvalidToken();

    if (failures != 0)
        std::cerr << failures << " piece shadow display failure(s)\n";
    return failures == 0 ? 0 : 1;
}
