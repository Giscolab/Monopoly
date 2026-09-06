#include "PieceMovePlan.hpp"

#include <array>
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

    data::DataTag tag(const PieceMoveInstruction& instruction)
    {
        return data::dataTag(instruction.sequence);
    }

    bool isThreeD(const PieceMoveInstruction& instruction)
    {
        return instruction.sequence == data::EmptyDataId ||
            data::dataGroup(instruction.sequence) ==
                data::legacyGroupValue(data::LegacyGroupId::ThreeD);
    }

    void testBackwardsAndSpecialJumps()
    {
        const auto back = planTokenMove(
            actions::Type::NotifyMoveBackwards, 2, 12, 9, true);
        expect(back && back->instructions.size() == 1,
            "back-three notification produces the single historical stack item");
        if (back)
        {
            const auto& item = back->instructions.front();
            expect(!item.cameraOnly && item.startSquare == 9 &&
                !item.landingSquare && !item.corner,
                "back-three preserves source-unassigned landing/corner fields");
            expect(tag(item) == TokenBackThreeAnimationBaseTag +
                    2 * AnimationsPerToken && isThreeD(item),
                "back-three sequence uses token-specific DAT_3D tag");
        }

        const auto jail = planTokenMove(
            actions::Type::NotifyJumpToSquare, 0, 7, 40, true);
        expect(jail && jail->special == PieceMoveSpecial::GoToJail &&
            jail->instructions.empty(), "jump to jail bypasses TokenAnimStack");
        const auto leave = planTokenMove(
            actions::Type::NotifyJumpToSquare, 0, 40, 10, true);
        expect(leave && leave->special == PieceMoveSpecial::LeaveJail &&
            leave->instructions.empty(), "40 -> 10 jump preserves the get-out-of-jail special case");
        const auto offBoard = planTokenMove(
            actions::Type::NotifyJumpToSquare, 0, 7, 41, true);
        expect(offBoard &&
            offBoard->special == PieceMoveSpecial::OffBoardRequiresGameContext &&
            offBoard->instructions.empty(),
            "off-board jump stays explicit until bankrupt/victory game context exists");

        const auto backMid = planTokenMove(
            actions::Type::NotifyMoveBackwards, 0, 15, 20, true);
        const auto backLate = planTokenMove(
            actions::Type::NotifyMoveBackwards, 0, 35, 38, true);
        expect(backMid && backLate &&
            tag(backMid->instructions[0]) == TokenBackThreeAnimationBaseTag + 1 &&
            tag(backLate->instructions[0]) == TokenBackThreeAnimationBaseTag + 2,
            "back-three destination bands select a/b/c variants exactly");
    }

    void testStraightForwardAndAnimationToggle()
    {
        const auto straight = planTokenMove(
            actions::Type::NotifyMoveForwards, 1, 1, 6, true);
        expect(straight && straight->instructions.size() == 2,
            "straight forward move emits movement plus final camera item");
        if (straight)
        {
            const auto& move = straight->instructions[0];
            const auto& final = straight->instructions[1];
            expect(move.startSquare == 1 && move.landingSquare == 6 &&
                move.corner == false &&
                tag(move) == TokenMoveAnimationBaseTag + AnimationsPerToken + 5,
                "straight movement chooses token move base plus distance");
            expect(final.cameraOnly && final.sequence == data::EmptyDataId &&
                final.startSquare == 6 && final.landingSquare == 6,
                "forward plan always ends with explicit camera-only destination item");
        }

        const auto disabled = planTokenMove(
            actions::Type::NotifyMoveForwards, 1, 1, 6, false);
        expect(disabled && disabled->instructions.size() == 1 &&
            disabled->instructions[0].cameraOnly,
            "animations-off source path keeps only the final camera stack item");

        const auto cornerLanding = planTokenMove(
            actions::Type::NotifyMoveForwards, 0, 1, 10, true);
        expect(cornerLanding && cornerLanding->instructions.size() == 3,
            "non-ten-square landing on a corner stacks an additional CX0 spin");
        if (cornerLanding)
        {
            expect(tag(cornerLanding->instructions[0]) == TokenMoveAnimationBaseTag + 9 &&
                tag(cornerLanding->instructions[1]) == TokenCornerAnimationBaseTag &&
                cornerLanding->instructions[1].corner == true,
                "corner landing preserves move then zero-distance corner sequence order");
        }
    }

    void testCornerCrossingAndRandomChoices()
    {
        const auto direct = planTokenMove(
            actions::Type::NotifyMoveForwards, 0, 8, 11, true);
        expect(direct && direct->instructions.size() == 2 &&
            tag(direct->instructions[0]) == TokenCornerAnimationBaseTag + 3 &&
            direct->instructions[0].corner == true,
            "8 -> 11 uses the 2/1 kitty-corner animation without randomness");

        const std::array<std::uint8_t, 2> choices{0, 1};
        const auto randomPlan = planTokenMove(
            actions::Type::NotifyMoveForwards, 0, 5, 13, true, choices);
        expect(randomPlan && randomPlan->randomChoicesUsed >= 1,
            "multi-step corner approach consumes explicit rand()%2 choices");
        const auto missing = planTokenMove(
            actions::Type::NotifyMoveForwards, 0, 5, 13, true);
        expect(!missing && missing.error() == PieceMovePlanError::MissingRandomChoice,
            "planner refuses to fabricate rand()%2 when source requires a choice");
    }

    void testOffBoardPlans()
    {
        const auto bankrupt = planOffBoardMove(1, 7, OffBoardOutcome::Bankrupt);
        expect(bankrupt && bankrupt->special == PieceMoveSpecial::OffBoardBankrupt &&
            bankrupt->instructions.size() == 4,
            "bankrupt off-board plan emits two idles, blast and final camera");
        if (bankrupt)
        {
            const auto tokenBase = TokenMoveAnimationBaseTag + AnimationsPerToken;
            expect(tag(bankrupt->instructions[0]) == tokenBase &&
                tag(bankrupt->instructions[1]) == tokenBase &&
                tag(bankrupt->instructions[2]) == tokenBase + 8,
                "bankrupt stack preserves token-specific idle/idle/blast sequence tags");
            expect(bankrupt->instructions[3].cameraOnly &&
                bankrupt->instructions[3].camera == BoardCameraView::TopDownSoccer &&
                bankrupt->instructions[3].startSquare == 41,
                "bankrupt stack ends on historical preset camera view index 1");
        }

        const auto victory = planOffBoardMove(0, 35, OffBoardOutcome::Victory);
        expect(victory && victory->special == PieceMoveSpecial::OffBoardVictory &&
            victory->instructions.size() == 6 &&
            victory->loopBegin == 2 && victory->loopEnd == 6,
            "victory plan reaches next corner, spins, then marks four loop segments");
        if (victory)
        {
            expect(tag(victory->instructions[0]) == TokenMoveAnimationBaseTag + 5 &&
                tag(victory->instructions[1]) == TokenCornerAnimationBaseTag,
                "victory approach uses exact five-square move then CX0 corner sequence");
            bool fourTens = true;
            for (std::size_t i = 2; i != 6; ++i)
                fourTens = fourTens &&
                    tag(victory->instructions[i]) == TokenMoveAnimationBaseTag + 10;
            expect(fourTens,
                "victory loop consists of four exact ten-square move sequences");
        }

        const auto cornerVictory = planOffBoardMove(0, 30, OffBoardOutcome::Victory);
        expect(cornerVictory && cornerVictory->instructions.size() == 5 &&
            cornerVictory->loopBegin == 1 && cornerVictory->loopEnd == 5 &&
            tag(cornerVictory->instructions[0]) == TokenMoveAnimationBaseTag + 10,
            "victory starting on a corner uses one ten-square approach and no redundant spin");

        expect(!planOffBoardMove(rules::MaxTokens, 0, OffBoardOutcome::Bankrupt) &&
            !planOffBoardMove(0, 40, OffBoardOutcome::Victory),
            "off-board planner rejects invalid token and non-board source square");
    }
    void testValidation()
    {
        expect(!planTokenMove(actions::Type::NotifyMoveForwards,
                rules::MaxTokens, 0, 1, true),
            "token outside RULE token enum is rejected");
        expect(!planTokenMove(actions::Type::NotifyMoveForwards,
                0, -1, 1, true) &&
            !planTokenMove(actions::Type::NotifyMoveForwards,
                0, 0, 42, true),
            "squares outside the 0..41 display contract are rejected");
        const auto unsupported = planTokenMove(
            actions::Type::NotifyDiceRolled, 0, 0, 1, true);
        expect(!unsupported &&
            unsupported.error() == PieceMovePlanError::UnsupportedNotification,
            "non-movement notification cannot enter the move planner");
    }
}

int main()
{
    testBackwardsAndSpecialJumps();
    testStraightForwardAndAnimationToggle();
    testCornerCrossingAndRandomChoices();
    testOffBoardPlans();
    testValidation();
    std::cout << (failures ? "Piece move-plan tests FAILED\n" :
        "Piece move-plan tests passed\n");
    return failures ? 1 : 0;
}
