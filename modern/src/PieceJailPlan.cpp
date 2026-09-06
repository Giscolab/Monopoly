#include "PieceJailPlan.hpp"

#include "PiecePlacement.hpp"

#include <cmath>

namespace monopoly::pieces
{
    namespace
    {
        [[nodiscard]] std::expected<InterpolationVec3, PieceJailPlanError>
        point(std::uint8_t square) noexcept
        {
            const auto pose = tokenOrientation(square);
            if (!pose) return std::unexpected(PieceJailPlanError::MissingGeometry);
            return InterpolationVec3{pose->x, pose->y, pose->z};
        }

        [[nodiscard]] std::uint64_t paddyDistanceTicks(
            std::uint8_t square, std::uint64_t base) noexcept
        {
            if (square > 10 && square < 30)
                return base + 5U * static_cast<std::uint64_t>(square - 10);
            if (square >= 30)
                return base + 5U * static_cast<std::uint64_t>(50 - square);
            return base + 5U * static_cast<std::uint64_t>(10 - square);
        }

        [[nodiscard]] bool add(PieceInterpolationPath& path, std::size_t index,
            InterpolationVec3 a, InterpolationVec3 b,
            InterpolationVec3 c, InterpolationVec3 d,
            float tangent = BezierTangentDelta) noexcept
        {
            return addBezierSegment(path, index, a, b, c, d, tangent);
        }

        [[nodiscard]] InterpolationVec3 normalize(InterpolationVec3 value) noexcept
        {
            const float length = std::sqrt(value.x * value.x +
                value.y * value.y + value.z * value.z);
            if (length <= 0.000001F) return {};
            return value * (1.0F / length);
        }
    }

    std::expected<PieceJailRoutePlan, PieceJailPlanError>
    planPaddyToToken(std::uint8_t tokenSquare, std::uint64_t startTick,
        std::uint8_t randomBit)
    {
        if (tokenSquare >= 40)
            return std::unexpected(PieceJailPlanError::InvalidSquare);

        PieceJailRoutePlan result{};
        result.loadSquare = static_cast<std::uint8_t>((tokenSquare + 1U) % 40U);
        result.path.startTick = startTick;
        result.path.endTick = startTick + paddyDistanceTicks(tokenSquare, 60U);

        if (tokenSquare == 10)
        {
            result.camera = BoardCameraView::FiveTiles03;
            result.skipMotion = true;
            result.path.endTick = startTick;
            return result;
        }

        const auto token = point(tokenSquare);
        if (!token) return std::unexpected(token.error());
        const auto side = tokenSquare / 10U;

        if (side == 0)
        {
            const auto p9 = point(9);
            if (!p9) return std::unexpected(p9.error());
            auto v1 = *p9; v1.z += 59.0F;
            auto t4 = *token; t4.z += 39.0F;
            auto v4 = t4; v4.z += 19.0F;
            auto v3 = v4; v4.x -= 32.0F; v3.x += 3.0F;
            const auto v2 = 0.5F * (v1 + v3);
            if (!add(result.path, 0, v1, v2, v3, v4))
                return std::unexpected(PieceJailPlanError::TooManyBezierSegments);
            auto t3 = t4; t3.z += 8.0F;
            if (!add(result.path, 1, v4, v3, t3, t4, -BezierTangentDelta))
                return std::unexpected(PieceJailPlanError::TooManyBezierSegments);
            result.camera = tokenSquare > 5 ? BoardCameraView::FiveTiles02 :
                pickCameraFor15Squares(tokenSquare);
            return result;
        }

        if (side == 1)
        {
            const auto jail = point(10);
            if (!jail) return std::unexpected(jail.error());
            auto v1 = *jail; v1.x -= 10.0F;
            auto t4 = *token; t4.x += 39.0F;
            auto v4 = t4; v4.x -= 78.0F;
            const auto v2 = 0.7F * v1 + 0.3F * v4;
            auto v3 = 0.3F * v1 + 0.7F * v4;
            if (!add(result.path, 0, v1, v2, v3, v4))
                return std::unexpected(PieceJailPlanError::TooManyBezierSegments);

            auto t1 = v4;
            auto t2 = v4; t2.x += 8.0F;
            v4 = t4; v4.x -= 39.0F; v4.z += 22.0F;
            v3 = v4; v3.x -= 8.0F;
            if (!add(result.path, 1, t1, t2, v3, v4))
                return std::unexpected(PieceJailPlanError::TooManyBezierSegments);
            auto t3 = t4; t3.x -= 8.0F;
            if (!addBezierSegmentSmooth(result.path, 2, t3, t4))
                return std::unexpected(PieceJailPlanError::TooManyBezierSegments);
            result.camera = tokenSquare < 15 ? BoardCameraView::FiveTiles03 :
                BoardCameraView::FifteenTiles03;
            return result;
        }

        if (side == 2)
        {
            const auto jail = point(10);
            const auto freeParking = point(20);
            if (!jail) return std::unexpected(jail.error());
            if (!freeParking) return std::unexpected(freeParking.error());
            auto v1 = *jail;
            auto v4 = *freeParking;
            auto v3 = v4; v3.z += 26.0F; v3.x += 5.0F;
            const auto v2 = 0.5F * (v3 + v1);
            if (!add(result.path, 0, v1, v2, v3, v4))
                return std::unexpected(PieceJailPlanError::TooManyBezierSegments);

            v1 = v4;
            auto t4 = *token; t4.z -= 39.0F;
            v4 = t4; v4.z += 78.0F;
            auto b2 = 0.7F * v1 + 0.3F * v4;
            v3 = 0.3F * v1 + 0.7F * v4;
            if (!add(result.path, 1, v1, b2, v3, v4))
                return std::unexpected(PieceJailPlanError::TooManyBezierSegments);
            auto t1 = v4;
            auto t2 = v4; t2.z -= 8.0F;
            v4 = t4; v4.z += 39.0F; v4.y += 25.0F;
            v3 = v4; v3.z += 8.0F;
            if (!add(result.path, 2, t1, t2, v3, v4))
                return std::unexpected(PieceJailPlanError::TooManyBezierSegments);
            auto t3 = t4; t3.z += 8.0F;
            if (!addBezierSegmentSmooth(result.path, 3, t3, t4))
                return std::unexpected(PieceJailPlanError::TooManyBezierSegments);
            result.camera = tokenSquare < 22 ? BoardCameraView::CornerFreeParking :
                tokenSquare < 25 ? BoardCameraView::FiveTiles05 :
                BoardCameraView::FifteenTiles06;
            return result;
        }

        const auto p9 = point(9);
        const auto go = point(0);
        const auto p39 = point(39);
        if (!p9) return std::unexpected(p9.error());
        if (!go) return std::unexpected(go.error());
        if (!p39) return std::unexpected(p39.error());
        auto v1 = *p9; v1.z += 39.0F;
        auto v3 = *go;
        auto v4 = v3;
        auto v2 = 0.5F * (v1 + v3); v2.x += 5.0F;
        v4.x -= 16.0F; v4.z -= 16.0F;
        if (!add(result.path, 0, v1, v2, v3, v4))
            return std::unexpected(PieceJailPlanError::TooManyBezierSegments);

        auto t4 = *token; t4.x -= 39.0F;
        auto t2 = *p39; t2.x -= 39.0F;
        auto t3 = 0.5F * (t4 + v3);
        if (tokenSquare == 39) { t2.x -= 10.0F; t3.x -= 5.0F; }
        if (!add(result.path, 1, v4, t2, t3, t4, -BezierTangentDelta))
            return std::unexpected(PieceJailPlanError::TooManyBezierSegments);
        result.camera = tokenSquare == 30 ?
            ((randomBit & 1U) == 0U ? BoardCameraView::CornerGoToJail :
                BoardCameraView::FifteenTiles09) :
            (tokenSquare < 35 ? BoardCameraView::FiveTiles07 :
                BoardCameraView::FiveTiles08);
        return result;
    }

    std::expected<PieceJailRoutePlan, PieceJailPlanError>
    planPaddyToJail(std::uint8_t tokenSquare,
        InterpolationVec3 startLocation, std::uint64_t startTick)
    {
        if (tokenSquare >= 40)
            return std::unexpected(PieceJailPlanError::InvalidSquare);
        const auto drop = point(11);
        if (!drop) return std::unexpected(drop.error());

        PieceJailRoutePlan result{};
        result.loadSquare = 11;
        result.path.startTick = startTick;
        result.path.endTick = startTick + paddyDistanceTicks(tokenSquare, 40U);
        const auto side = tokenSquare / 10U;
        const auto v1 = startLocation;
        const auto t4 = *drop;

        if (side == 0)
        {
            const auto jail = point(10);
            if (!jail) return std::unexpected(jail.error());
            auto v3 = *jail; v3.x -= 20.0F; v3.z -= 5.0F;
            const auto v2 = 0.5F * (v1 + v3);
            if (!add(result.path, 0, v1, v2, v3, t4))
                return std::unexpected(PieceJailPlanError::TooManyBezierSegments);
            result.camera = BoardCameraView::CornerJail;
            return result;
        }

        if (side == 1)
        {
            const auto v2 = 0.7F * v1 + 0.3F * t4;
            const auto v3 = 0.3F * v1 + 0.7F * t4;
            if (!add(result.path, 0, v1, v2, v3, t4, -BezierTangentDelta))
                return std::unexpected(PieceJailPlanError::TooManyBezierSegments);
            result.camera = tokenSquare < 14 ? BoardCameraView::ThreeTiles04 :
                BoardCameraView::CornerJail;
            return result;
        }

        if (side == 2)
        {
            const auto freeParking = point(20);
            if (!freeParking) return std::unexpected(freeParking.error());
            auto v4 = *freeParking; v4.x += 10.0F; v4.z += 26.0F;
            auto v3 = *freeParking; v3.z += 16.0F;
            const auto v2 = 0.6F * v1 + 0.4F * v3;
            if (!add(result.path, 0, v1, v2, v3, v4, -BezierTangentDelta))
                return std::unexpected(PieceJailPlanError::TooManyBezierSegments);

            const auto t1 = v4;
            const auto t2 = v3;
            v3 = t4; v3.x += 20.0F; v3.z -= 5.0F;
            v4 = t4; v4.z += 20.0F; v4.x += 20.0F;
            if (!add(result.path, 1, t1, t2, v3, v4))
                return std::unexpected(PieceJailPlanError::TooManyBezierSegments);

            auto t3 = t4; t3.x += 14.0F;
            const auto c1 = v4;
            const auto c2 = v4 + 14.0F * normalize(v3 - v4);
            if (!add(result.path, 2, c1, c2, t3, t4, -BezierTangentDelta))
                return std::unexpected(PieceJailPlanError::TooManyBezierSegments);
            result.path.lengths[2] *= 2.0F;
            result.camera = BoardCameraView::FifteenTiles03;
            return result;
        }

        const auto go = point(0);
        const auto jail = point(10);
        if (!go) return std::unexpected(go.error());
        if (!jail) return std::unexpected(jail.error());
        auto v4 = *go;
        auto v3 = v4; v3.z -= 24.0F; v3.x -= 5.0F;
        const auto v2 = 0.6F * v1 + 0.4F * v3;
        if (!add(result.path, 0, v1, v2, v3, v4))
            return std::unexpected(PieceJailPlanError::TooManyBezierSegments);
        auto t3 = *jail; t3.x -= 34.0F; t3.z += 6.0F;
        const auto t2 = 0.5F * (v4 + t3);
        if (!add(result.path, 1, v4, t2, t3, t4))
            return std::unexpected(PieceJailPlanError::TooManyBezierSegments);
        result.camera = tokenSquare < 35 ? BoardCameraView::FifteenTiles12 :
            BoardCameraView::FiveTiles03;
        return result;
    }
}
