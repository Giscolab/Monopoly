#include "PieceMoveIngress.hpp"

#include <cstdlib>
#include <utility>
#include <vector>

namespace monopoly::pieces
{
    namespace
    {
        [[nodiscard]] bool movementAction(actions::Type action) noexcept
        {
            return action == actions::Type::NotifyMoveForwards ||
                action == actions::Type::NotifyMoveBackwards ||
                action == actions::Type::NotifyJumpToSquare;
        }

        [[nodiscard]] PieceMoveIngressError plannerError(
            PieceMovePlanError error) noexcept
        {
            return {PieceMoveIngressErrorCode::PlannerFailure, error};
        }
    }

    PieceMoveIngress::PieceMoveIngress()
        : PieceMoveIngress([] { return static_cast<std::uint8_t>(std::rand() & 1); })
    {
    }

    PieceMoveIngress::PieceMoveIngress(RandomBitSource randomBit)
        : randomBit_(std::move(randomBit))
    {
    }
    void PieceMoveIngress::reset() noexcept
    {
        pendingPlan_.reset();
        pendingSpecial_.reset();
    }

    std::expected<PieceMovePlan, PieceMoveIngressError>
    PieceMoveIngress::buildPlan(actions::Type action, std::uint8_t token,
        std::int32_t before, std::int32_t after, bool animationsEnabled)
    {
        std::vector<std::uint8_t> choices;
        choices.reserve(TokenAnimationItemLimit);
        for (;;)
        {
            auto planned = planTokenMove(action, token, before, after,
                animationsEnabled, choices);
            if (planned) return std::move(*planned);
            if (planned.error() != PieceMovePlanError::MissingRandomChoice)
                return std::unexpected(plannerError(planned.error()));
            if (choices.size() >= TokenAnimationItemLimit)
                return std::unexpected(plannerError(
                    PieceMovePlanError::MissingRandomChoice));
            choices.push_back(randomBit_ ?
                static_cast<std::uint8_t>(randomBit_() & 1U) : 0U);
        }
    }

    std::optional<PieceMovePlan> PieceMoveIngress::takePlan()
    {
        auto result = std::move(pendingPlan_);
        pendingPlan_.reset();
        return result;
    }
    std::optional<PieceMoveSpecialRequest> PieceMoveIngress::takeSpecial()
    {
        auto result = std::move(pendingSpecial_);
        pendingSpecial_.reset();
        return result;
    }

    std::expected<PieceMoveIngressResult, PieceMoveIngressError>
    PieceMoveIngress::process(rules::GameState& uiState,
        const actions::Message& message, bool animationsEnabled)
    {
        if (!movementAction(message.action))
            return std::unexpected(PieceMoveIngressError{
                PieceMoveIngressErrorCode::UnsupportedMessage, std::nullopt});
        if (pendingPlan_ || pendingSpecial_)
            return std::unexpected(PieceMoveIngressError{
                PieceMoveIngressErrorCode::Busy, std::nullopt});
        if (message.numberC < 0 ||
            message.numberC >= static_cast<std::int64_t>(rules::MaxPlayers))
            return std::unexpected(PieceMoveIngressError{
                PieceMoveIngressErrorCode::InvalidPlayer, std::nullopt});

        const auto player = static_cast<rules::PlayerNumber>(message.numberC);
        const auto before = static_cast<std::int32_t>(uiState.players[player].currentSquare);
        const auto after64 = message.numberA;
        if (after64 < 0 || after64 >= static_cast<std::int64_t>(rules::SquareCount))
            return std::unexpected(plannerError(PieceMovePlanError::InvalidSquare));
        const auto after = static_cast<std::int32_t>(after64);
        const auto token = uiState.players[player].token;

        auto planned = buildPlan(message.action, token, before, after,
            animationsEnabled);
        if (!planned) return std::unexpected(planned.error());
        auto plan = std::move(*planned);
        if (plan.special == PieceMoveSpecial::OffBoardRequiresGameContext)
        {
            const auto count = static_cast<std::size_t>(uiState.numberOfPlayers);
            if (count == 0 || count > rules::MaxPlayers)
                return std::unexpected(PieceMoveIngressError{
                    PieceMoveIngressErrorCode::InvalidGameProjection, std::nullopt});
            std::size_t alreadyOffBoard{};
            for (std::size_t i = 0; i < count; ++i)
                if (uiState.players[i].currentSquare == 41) ++alreadyOffBoard;
            const auto outcome = alreadyOffBoard >= count - 1 ?
                OffBoardOutcome::Victory : OffBoardOutcome::Bankrupt;
            auto offBoard = planOffBoardMove(token, before, outcome);
            if (!offBoard) return std::unexpected(plannerError(offBoard.error()));
            plan = std::move(*offBoard);
        }

        PieceMoveIngressResult result{};
        result.special = plan.special;
        result.sourceQueueLockRequired = !plan.instructions.empty() ||
            plan.special == PieceMoveSpecial::GoToJail;

        if (plan.special == PieceMoveSpecial::GoToJail ||
            plan.special == PieceMoveSpecial::LeaveJail)
        {
            pendingSpecial_ = PieceMoveSpecialRequest{
                plan.special, player, token, before, after};
        }
        else
        {
            pendingPlan_ = std::move(plan);
            result.planQueued = true;
        }

        if (after != 40)
        {
            uiState.players[player].currentSquare = static_cast<std::uint8_t>(after);
            result.projectionUpdated = true;
        }
        return result;
    }
}
