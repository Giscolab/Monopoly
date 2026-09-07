#include "BoardOwnershipHighlight.hpp"
#include "SyntheticSequenceResources.hpp"

#include <cmath>
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

    bool near(float left, float right) noexcept
    {
        return std::fabs(left - right) < 0.001F;
    }

    rules::GameState baseState()
    {
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.players[0].colour = 2;
        state.players[1].colour = 5;
        state.squares[1].owner = 0;
        state.squares[3].owner = 1;
        state.squares[3].mortgaged = true;
        return state;
    }

    void testPlannerIdsAndModes()
    {
        require(boarddisplay::ownershipPropertyIndex(1) == 0 &&
                boarddisplay::ownershipPropertyIndex(39) == 27 &&
                boarddisplay::ownershipPropertyIndex(2) == -1,
            "ownership property conversion preserves exact 28-ownable order");

        const auto state = baseState();
        const auto hidden = boarddisplay::planOwnershipHighlights(
            state, display::Screen2D::PlayerSelect, false,
            pieces::BoardCameraView::TopDownSoccer);
        require(hidden && !(*hidden)[1] && !(*hidden)[3],
            "ownership highlights are absent outside DISPLAY_IsBoardVisible");

        const auto twoD = boarddisplay::planOwnershipHighlights(
            state, display::Screen2D::Main, false,
            pieces::BoardCameraView::TopDownSoccer);
        require(twoD && (*twoD)[1] && (*twoD)[3],
            "Main software-board fallback plans both owned properties");
        require(data::dataGroup((*twoD)[1]->sequence) ==
                    data::legacyGroupValue(data::LegacyGroupId::Board) &&
                data::dataTag((*twoD)[1]->sequence) == 0x0404 &&
                (*twoD)[1]->priority == 13 &&
                (*twoD)[1]->kind == boarddisplay::OwnershipHighlightKind::Bitmap2D,
            "normal Main 2D highlight uses DAT_BOARD camera/property/colour formula");
        require(data::dataGroup((*twoD)[3]->sequence) ==
                    data::legacyGroupValue(data::LegacyGroupId::Board2) &&
                data::dataTag((*twoD)[3]->sequence) == 0x00B3 &&
                (*twoD)[3]->priority == 15,
            "mortgaged Main 2D highlight uses DAT_BOARD2 and priority 12+square");

        const auto threeD = boarddisplay::planOwnershipHighlights(
            state, display::Screen2D::Main, true,
            pieces::BoardCameraView::TopDownSoccer);
        require(threeD && data::dataTag((*threeD)[1]->sequence) == 0x00F1 &&
                data::dataTag((*threeD)[3]->sequence) == 0x00EE &&
                (*threeD)[1]->kind == boarddisplay::OwnershipHighlightKind::Mesh3D,
            "Main 3D ownership uses exact HMD_mnoi1/HMD_mnmi1 colour offsets");

        const auto portfolio = boarddisplay::planOwnershipHighlights(
            state, display::Screen2D::Portfolio, false,
            pieces::BoardCameraView::TopDownSoccer);
        require(portfolio && (*portfolio)[1] &&
                (*portfolio)[1]->kind == boarddisplay::OwnershipHighlightKind::Mesh3D &&
                data::dataTag((*portfolio)[1]->sequence) == 0x00F1,
            "Portfolio forces HMD ownership highlights even when board3DOn is false");

        auto badColour = state;
        badColour.players[0].colour = 6;
        require(!boarddisplay::planOwnershipHighlights(
                    badColour, display::Screen2D::Main, false,
                    pieces::BoardCameraView::TopDownSoccer),
            "ownership colour outside legacy 0..5 range is rejected");

        require(!boarddisplay::planOwnershipHighlights(
                    state, display::Screen2D::Main, false,
                    static_cast<pieces::BoardCameraView>(39)),
            "Main 2D ownership rejects camera outside legacy 0..38 range");

        auto corrupt = state;
        corrupt.squares[2].owner = 0;
        require(!boarddisplay::planOwnershipHighlights(
                    corrupt, display::Screen2D::Main, false,
                    pieces::BoardCameraView::TopDownSoccer),
            "2D ownership fails closed on an owned non-ownable square");
    }

    void testThreeDTransforms()
    {
        const auto side0 = boarddisplay::ownershipHighlight3DTransform(1);
        require(near(side0.values[0], 0.10F) &&
                near(side0.values[12], -8.0F) && near(side0.values[14], 85.0F),
            "side 0 highlight preserves yaw 0, scale .10 and offsets -8/-19");

        const auto side1 = boarddisplay::ownershipHighlight3DTransform(11);
        require(near(side1.values[0], 0.0F) && near(side1.values[2], -0.10F) &&
                near(side1.values[8], 0.10F) &&
                near(side1.values[12], 85.0F) && near(side1.values[14], 494.0F),
            "side 1 highlight preserves +pi/2 yaw and exact board offsets");

        const auto side2 = boarddisplay::ownershipHighlight3DTransform(21);
        require(near(side2.values[0], -0.10F) && near(side2.values[10], -0.10F) &&
                near(side2.values[12], 494.0F) && near(side2.values[14], 401.0F),
            "side 2 highlight preserves pi yaw and exact board offsets");

        const auto side3 = boarddisplay::ownershipHighlight3DTransform(31);
        require(near(side3.values[2], 0.10F) && near(side3.values[8], -0.10F) &&
                near(side3.values[12], 401.0F) && near(side3.values[14], -8.0F),
            "side 3 highlight preserves -pi/2 yaw and exact board offsets");
    }

    void testPlaybackLifecycle()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        boarddisplay::OwnershipHighlightPlayback highlights;
        const auto state = baseState();
        const auto twoD = boarddisplay::planOwnershipHighlights(
            state, display::Screen2D::Main, false,
            pieces::BoardCameraView::TopDownSoccer).value();

        require(highlights.sync(twoD, playback) &&
                playback.commands().pendingCount() == 4 && playback.update(0),
            "initial two-property 2D ownership transition queues two StartCXYSlot pairs");
        require(playback.world2D().size() == 2 && playback.world().size() == 0,
            "Main 2D ownership reaches Overlay2D only");
        const auto normal2D = data::packDataId(data::LegacyGroupId::Board, 0x0404);
        const auto mortgage2D = data::packDataId(data::LegacyGroupId::Board2, 0x00B3);
        const auto normalNodes = playback.runtime().matching(normal2D, 13, false);
        const auto mortgageNodes = playback.runtime().matching(mortgage2D, 15, false);
        const auto* normalObject = normalNodes.empty() ? nullptr :
            playback.world2D().find(normalNodes.front());
        const auto* mortgageObject = mortgageNodes.empty() ? nullptr :
            playback.world2D().find(mortgageNodes.front());
        require(normalObject && mortgageObject &&
                near(normalObject->worldTransform.values[6], -7.0F) &&
                near(normalObject->worldTransform.values[7], 13.0F) &&
                near(mortgageObject->worldTransform.values[6], -7.0F) &&
                near(mortgageObject->worldTransform.values[7], 13.0F),
            "2D ownership StartCXYSlot preserves each UAP embedded origin");
        require(highlights.sync(twoD, playback) &&
                playback.commands().pendingCount() == 0,
            "unchanged ownership plan queues no corrective commands");

        const auto threeD = boarddisplay::planOwnershipHighlights(
            state, display::Screen2D::Main, true,
            pieces::BoardCameraView::TopDownSoccer).value();
        require(highlights.sync(threeD, playback) &&
                playback.commands().pendingCount() == 6 && playback.update(1),
            "2D to 3D ownership replacement queues Stop/Start/Move per square");
        require(playback.world2D().size() == 0 && playback.world().size() == 2,
            "ownership replacement removes UAP leaves and publishes two HMD leaves");
        const auto normal3D = data::packDataId(data::LegacyGroupId::ThreeD, 0x00F1);
        const auto mortgage3D = data::packDataId(data::LegacyGroupId::ThreeD, 0x00EE);
        const auto normalInfo = playback.runtime().info(normal3D, 13, false);
        const auto mortgageInfo = playback.runtime().info(mortgage3D, 15, false);
        require(normalInfo && mortgageInfo && normalInfo->sequenceToWorldTransformation &&
                mortgageInfo->sequenceToWorldTransformation &&
                near(normalInfo->sequenceToWorldTransformation->values[12], -8.0F) &&
                near(normalInfo->sequenceToWorldTransformation->values[14], 85.0F) &&
                near(mortgageInfo->sequenceToWorldTransformation->values[12], -8.0F) &&
                near(mortgageInfo->sequenceToWorldTransformation->values[14], 164.0F),
            "3D ownership runtime preserves exact UDBOARD_GetHighlightPosition transforms");

        const auto hidden = boarddisplay::planOwnershipHighlights(
            state, display::Screen2D::PlayerSelect, false,
            pieces::BoardCameraView::TopDownSoccer).value();
        require(highlights.sync(hidden, playback) &&
                playback.commands().pendingCount() == 2 && playback.update(2) &&
                playback.world().size() == 0 && playback.world2D().size() == 0,
            "leaving board-visible views stops every ownership highlight");
    }

    void testPlaybackFailureIsTransactional()
    {
        SyntheticSequenceResources resources;
        const auto state = baseState();
        const auto valid = boarddisplay::planOwnershipHighlights(
            state, display::Screen2D::Main, false,
            pieces::BoardCameraView::TopDownSoccer).value();

        engine::SequencePlayback missing(resources.service.snapshot());
        boarddisplay::OwnershipHighlightPlayback missingHighlights;
        auto missingPlan = valid;
        missingPlan[1]->sequence =
            data::packDataId(data::LegacyGroupId::Board, 0xFFFF);
        const auto missingResult = missingHighlights.sync(missingPlan, missing);
        require(!missingResult && missing.commands().pendingCount() == 0 &&
                missingHighlights.current(1) == data::EmptyDataId &&
                missingHighlights.current(3) == data::EmptyDataId,
            "missing ownership resource queues no partial board transition");

        engine::SequencePlayback full(resources.service.snapshot());
        boarddisplay::OwnershipHighlightPlayback fullHighlights;
        for (std::size_t count = 0;
             count < sequence::SequenceCommandQueue::Capacity - 3; ++count)
        {
            if (!full.commands().enqueue(sequence::StopSequenceCommand{1, 0, false}))
                throw std::runtime_error("FIFO setup failed");
        }
        const auto noRoom = fullHighlights.sync(valid, full);
        require(!noRoom &&
                full.commands().pendingCount() ==
                    sequence::SequenceCommandQueue::Capacity - 3 &&
                fullHighlights.current(1) == data::EmptyDataId &&
                fullHighlights.current(3) == data::EmptyDataId,
            "insufficient FIFO preserves complete ownership playback state");
    }
}

int main()
{
    try
    {
        testPlannerIdsAndModes();
        testThreeDTransforms();
        testPlaybackLifecycle();
        testPlaybackFailureIsTransactional();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
