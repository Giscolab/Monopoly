#include "PieceMovePlan.hpp"

#include "RuleTypes.hpp"

#include <cstddef>
#include <utility>

namespace monopoly::pieces
{
    namespace
    {
        constexpr std::int32_t InJail = 40;
        constexpr std::int32_t OffBoard = 41;

        data::DataId animationId(
            data::DataTag base,
            std::uint8_t token,
            std::uint16_t offset = 0) noexcept
        {
            const auto tag = static_cast<data::DataTag>(
                static_cast<std::uint32_t>(base) +
                static_cast<std::uint32_t>(token) * AnimationsPerToken +
                offset);
            return data::packDataId(data::LegacyGroupId::ThreeD, tag);
        }

        std::expected<void, PieceMovePlanError> append(
            PieceMovePlan& plan, PieceMoveInstruction instruction)
        {
            if (plan.instructions.size() >= TokenAnimationItemLimit)
                return std::unexpected(PieceMovePlanError::AnimationStackOverflow);
            plan.instructions.push_back(std::move(instruction));
            return {};
        }

        std::expected<std::int32_t, PieceMovePlanError> randomBit(
            std::span<const std::uint8_t> choices, std::size_t& cursor)
        {
            if (cursor >= choices.size())
                return std::unexpected(PieceMovePlanError::MissingRandomChoice);
            return static_cast<std::int32_t>(choices[cursor++] & 1U);
        }

        PieceMoveInstruction movementInstruction(
            BoardCameraView camera,
            data::DataId sequence,
            std::int32_t start,
            std::int32_t landing,
            bool corner) noexcept
        {
            return {false, 0, camera, sequence, start, landing, corner};
        }

        PieceMoveInstruction finalCameraInstruction(
            BoardCameraView camera, std::int32_t square) noexcept
        {
            return {true, 0, camera, data::EmptyDataId,
                square, square, false};
        }

        bool validBoardSquare(std::int32_t square) noexcept
        {
            return square >= 0 && square <= OffBoard;
        }
    }

    std::expected<PieceMovePlan, PieceMovePlanError> planTokenMove(
        actions::Type notification,
        std::uint8_t token,
        std::int32_t before,
        std::int32_t after,
        bool animationsEnabled,
        std::span<const std::uint8_t> randomChoices)
    {
        if (token >= rules::MaxTokens)
            return std::unexpected(PieceMovePlanError::InvalidToken);
        if (!validBoardSquare(before) || !validBoardSquare(after))
            return std::unexpected(PieceMovePlanError::InvalidSquare);
        if (notification != actions::Type::NotifyMoveForwards &&
            notification != actions::Type::NotifyMoveBackwards &&
            notification != actions::Type::NotifyJumpToSquare)
            return std::unexpected(PieceMovePlanError::UnsupportedNotification);

        PieceMovePlan plan{};
        plan.sourceSquare = before;
        plan.destinationSquare = after;
        const auto finalCamera = pickCameraFor3Squares(after);
        const auto startingCamera = pickCameraFor15Squares(before);
        std::size_t randomCursor{};

        if (notification == actions::Type::NotifyMoveBackwards)
        {
            std::uint16_t variant{};
            if (after > 10) variant = 1;
            if (after > 30) variant = 2;
            PieceMoveInstruction instruction{};
            instruction.camera = finalCamera;
            instruction.sequence = animationId(
                TokenBackThreeAnimationBaseTag, token, variant);
            instruction.startSquare = after;
            // Source leaves landing and IsCorner untouched in this branch.
            if (const auto added = append(plan, instruction); !added)
                return std::unexpected(added.error());
            return plan;
        }

        if (notification == actions::Type::NotifyJumpToSquare)
        {
            if (after == InJail)
            {
                plan.special = PieceMoveSpecial::GoToJail;
                return plan;
            }
            if (after == OffBoard)
            {
                plan.special = PieceMoveSpecial::OffBoardRequiresGameContext;
                return plan;
            }
            if (before == InJail && after == 10)
            {
                plan.special = PieceMoveSpecial::LeaveJail;
                return plan;
            }
            // Other jump notifications deliberately fall through to the
            // same forward planner used by the source.
        }

        if (animationsEnabled)
        {
            const auto sideBefore = before < 40 ? before / 10 : 1;
            auto sideAfter = after < 40 ? (after - 1) / 10 : 0;
            if (after - 1 < 0) sideAfter = 3;
            auto cornersPassed = sideAfter - sideBefore;
            if (cornersPassed < 0) cornersPassed += 4;
            if (cornersPassed == 0 && before > after) cornersPassed = 4;

            auto square = before;
            bool first = true;
            while (square != after &&
                plan.instructions.size() < TokenAnimationItemLimit - 1)
            {
                auto destination = square + (10 - (square % 10));
                if (destination > after && square < after) destination = after;
                if (destination == 40) destination = 0;

                if (destination != after)
                {
                    const bool kittyCornerLanding =
                        destination == after - 1 || destination == after - 2 ||
                        destination == after - 1 + 40 || destination == after - 2 + 40;
                    if (kittyCornerLanding)
                    {
                        if (10 - (square % 10) > 2)
                        {
                            const auto bit = randomBit(randomChoices, randomCursor);
                            if (!bit) return std::unexpected(bit.error());
                            destination -= 1 + *bit;
                        }
                        else
                        {
                            destination = after;
                        }
                    }
                    else if (cornersPassed == 1)
                    {
                        const auto bit = randomBit(randomChoices, randomCursor);
                        if (!bit) return std::unexpected(bit.error());
                        if (10 - (square % 10) > 2)
                            destination -= 1 + *bit;
                        else
                            destination += 1 + *bit;
                    }
                    while (destination < 0) destination += 40;
                    while (destination >= 40) destination -= 40;
                }

                auto distance = destination - square;
                if (distance < 0) distance += 40;
                const auto camera = first ? startingCamera :
                    pickGoodCamera(square, distance);

                const bool crossesCorner =
                    ((destination + 9) / 10) % 4 != ((square + 10) / 10) % 4;
                if (crossesCorner)
                {
                    std::uint16_t cornerOffset{};
                    if (distance == 2) cornerOffset = 1;
                    else if (distance == 4) cornerOffset = 4;
                    else if (destination % 10 == 1) cornerOffset = 3;
                    else if (destination % 10 == 2) cornerOffset = 2;
                    else return std::unexpected(
                        PieceMovePlanError::UnsupportedCornerShape);

                    const auto added = append(plan, movementInstruction(
                        camera,
                        animationId(TokenCornerAnimationBaseTag, token, cornerOffset),
                        square, destination, true));
                    if (!added) return std::unexpected(added.error());
                }
                else
                {
                    const auto added = append(plan, movementInstruction(
                        camera,
                        animationId(TokenMoveAnimationBaseTag, token,
                            static_cast<std::uint16_t>(distance)),
                        square, destination, false));
                    if (!added) return std::unexpected(added.error());

                    if (destination % 10 == 0 && distance != 10)
                    {
                        const auto spin = append(plan, movementInstruction(
                            pickGoodCamera(destination, 0),
                            animationId(TokenCornerAnimationBaseTag, token),
                            destination, destination, true));
                        if (!spin) return std::unexpected(spin.error());
                    }
                }

                square += distance;
                while (square >= 40) square -= 40;
                first = false;
            }
        }

        if (const auto final = append(plan,
                finalCameraInstruction(finalCamera, after)); !final)
            return std::unexpected(final.error());
        plan.randomChoicesUsed = randomCursor;
        return plan;
    }

    std::expected<PieceMovePlan, PieceMovePlanError> planOffBoardMove(
        std::uint8_t token, std::int32_t before, OffBoardOutcome outcome)
    {
        if (token >= rules::MaxTokens)
            return std::unexpected(PieceMovePlanError::InvalidToken);
        if (before < 0 || before >= 40)
            return std::unexpected(PieceMovePlanError::InvalidSquare);

        PieceMovePlan plan{};
        plan.sourceSquare = before;
        plan.destinationSquare = OffBoard;
        if (outcome == OffBoardOutcome::Bankrupt)
        {
            plan.special = PieceMoveSpecial::OffBoardBankrupt;
            const auto camera = pickCameraFor3Squares(before);
            for (int i = 0; i != 2; ++i)
            {
                const auto added = append(plan, movementInstruction(camera,
                    animationId(TokenMoveAnimationBaseTag, token),
                    before, before, false));
                if (!added) return std::unexpected(added.error());
            }
            const auto blast = append(plan, movementInstruction(camera,
                animationId(TokenMoveAnimationBaseTag, token, 8),
                before, before, false));
            if (!blast) return std::unexpected(blast.error());
            const auto final = append(plan, finalCameraInstruction(
                BoardCameraView::TopDownSoccer, OffBoard));
            if (!final) return std::unexpected(final.error());
            return plan;
        }

        plan.special = PieceMoveSpecial::OffBoardVictory;
        auto square = before;
        auto distanceToCorner = 10 - (square % 10);
        if (distanceToCorner == 0) distanceToCorner = 10;

        const auto first = append(plan, movementInstruction(
            pickCameraFor15Squares(square),
            animationId(TokenMoveAnimationBaseTag, token,
                static_cast<std::uint16_t>(distanceToCorner)),
            square, square, false));
        if (!first) return std::unexpected(first.error());

        square += distanceToCorner;
        if (square >= 40) square = 0;

        if (distanceToCorner != 10)
        {
            const auto spin = append(plan, movementInstruction(
                pickCameraFor15Squares(square),
                animationId(TokenCornerAnimationBaseTag, token),
                square, square, true));
            if (!spin) return std::unexpected(spin.error());
        }

        plan.loopBegin = plan.instructions.size();
        for (int lap = 0; lap != 4; ++lap)
        {
            const auto loop = append(plan, movementInstruction(
                pickCameraFor15Squares(square),
                animationId(TokenMoveAnimationBaseTag, token, 10),
                square, square, false));
            if (!loop) return std::unexpected(loop.error());
            square += 10;
            if (square >= 40) square = 0;
        }
        plan.loopEnd = plan.instructions.size();
        return plan;
    }
}
