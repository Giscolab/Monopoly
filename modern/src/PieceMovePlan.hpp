#pragma once

#include "Actions.hpp"
#include "DataBanks.hpp"
#include "PieceCamera.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

namespace monopoly::pieces
{
    inline constexpr std::size_t TokenAnimationItemLimit = 12;
    inline constexpr data::DataTag TokenCornerAnimationBaseTag = 0x00F6;
    inline constexpr data::DataTag TokenMoveAnimationBaseTag = 0x010D;
    inline constexpr data::DataTag TokenBackThreeAnimationBaseTag = 0x0118;
    inline constexpr data::DataTag AnimationsPerToken = 0x0063;

    enum class PieceMoveSpecial : std::uint8_t
    {
        None,
        GoToJail,
        LeaveJail,
        OffBoardRequiresGameContext,
        OffBoardBankrupt,
        OffBoardVictory
    };

    enum class OffBoardOutcome : std::uint8_t
    {
        Bankrupt,
        Victory
    };

    enum class PieceMovePlanError : std::uint8_t
    {
        UnsupportedNotification,
        InvalidToken,
        InvalidSquare,
        MissingRandomChoice,
        AnimationStackOverflow,
        UnsupportedCornerShape
    };

    struct PieceMoveInstruction
    {
        bool cameraOnly{};
        std::uint32_t cameraDelay{};
        BoardCameraView camera{BoardCameraView::CornerGo};
        data::DataId sequence{data::EmptyDataId};
        std::int32_t startSquare{};
        std::optional<std::int32_t> landingSquare;
        std::optional<bool> corner;
    };

    struct PieceMovePlan
    {
        PieceMoveSpecial special{PieceMoveSpecial::None};
        std::int32_t sourceSquare{};
        std::int32_t destinationSquare{};
        std::vector<PieceMoveInstruction> instructions;
        std::optional<std::size_t> loopBegin;
        std::optional<std::size_t> loopEnd; // exclusive, mirrors TokenLoopAnimEnd
        std::size_t randomChoicesUsed{};
    };

    // Pure equivalent of UDPIECES_PlanMoveAnim's stack construction.
    // Each random byte supplies one source rand()%2 result via (byte & 1).
    [[nodiscard]] std::expected<PieceMovePlan, PieceMovePlanError> planTokenMove(
        actions::Type notification,
        std::uint8_t token,
        std::int32_t before,
        std::int32_t after,
        bool animationsEnabled,
        std::span<const std::uint8_t> randomChoices = {});

    // Pure off-board branch once the caller has decided bankrupt vs victory.
    [[nodiscard]] std::expected<PieceMovePlan, PieceMovePlanError> planOffBoardMove(
        std::uint8_t token,
        std::int32_t before,
        OffBoardOutcome outcome);
}
