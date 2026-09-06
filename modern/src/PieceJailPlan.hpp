#pragma once

#include "PieceCamera.hpp"
#include "PieceInterpolation.hpp"

#include <cstdint>
#include <expected>

namespace monopoly::pieces
{
    enum class PieceJailPlanError : std::uint8_t
    {
        InvalidSquare,
        MissingGeometry,
        TooManyBezierSegments
    };

    struct PieceJailRoutePlan
    {
        PieceInterpolationPath path;
        BoardCameraView camera{BoardCameraView::CornerJail};
        std::uint8_t loadSquare{};
        bool skipMotion{};
    };

    [[nodiscard]] std::expected<PieceJailRoutePlan, PieceJailPlanError>
    planPaddyToToken(std::uint8_t tokenSquare, std::uint64_t startTick,
        std::uint8_t randomBit = 0);

    [[nodiscard]] std::expected<PieceJailRoutePlan, PieceJailPlanError>
    planPaddyToJail(std::uint8_t tokenSquare,
        InterpolationVec3 startLocation, std::uint64_t startTick);
}
