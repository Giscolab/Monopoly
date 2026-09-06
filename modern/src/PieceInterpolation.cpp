#include "PieceInterpolation.hpp"

#include <algorithm>
#include <cmath>

namespace monopoly::pieces
{
    namespace
    {
        [[nodiscard]] float magnitude(InterpolationVec3 v) noexcept
        { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

        [[nodiscard]] InterpolationVec3 normalized(InterpolationVec3 v) noexcept
        {
            const float length = magnitude(v);
            if (length <= 0.00001F) return {};
            return v * (1.0F / length);
        }

        [[nodiscard]] InterpolationVec3 bezier(float ratio,
            const std::array<InterpolationVec3, 4>& p) noexcept
        {
            const float u = 1.0F - ratio;
            const float b0 = u * u * u;
            const float b1 = 3.0F * ratio * u * u;
            const float b2 = 3.0F * ratio * ratio * u;
            const float b3 = ratio * ratio * ratio;
            return b0 * p[0] + b1 * p[1] + b2 * p[2] + b3 * p[3];
        }

        [[nodiscard]] float adjustedRatio(float ratio, InterpolationSpeed speed) noexcept
        {
            ratio = std::clamp(ratio, 0.0F, 1.0F);
            if (speed == InterpolationSpeed::AccelerateInOut ||
                speed == InterpolationSpeed::FastAccelerateFlatMid)
            {
                constexpr float Pi = 3.14159265358979323846F;
                if (ratio < 0.5F)
                    return 0.5F - 0.5F * std::cos(ratio * Pi);
                return 0.5F + 0.5F * std::sin((ratio - 0.5F) * Pi);
            }
            return ratio;
        }
    }

    bool addBezierSegment(PieceInterpolationPath& path, std::size_t index,
        InterpolationVec3 a, InterpolationVec3 b,
        InterpolationVec3 c, InterpolationVec3 d,
        float tangentDelta) noexcept
    {
        if (index >= BezierMaxSegments) index = BezierMaxSegments - 1;
        path.segments[index] = {a, b, c, d};
        path.tangentDeltas[index] = tangentDelta;
        if (index == path.used && path.used < BezierMaxSegments)
            ++path.used;
        path.lengths[index] = magnitude(d - c) +
            magnitude(c - b) + magnitude(b - a);
        return true;
    }

    bool addBezierSegmentSmooth(PieceInterpolationPath& path, std::size_t index,
        InterpolationVec3 c, InterpolationVec3 d,
        float tangentDelta) noexcept
    {
        if (index >= BezierMaxSegments) index = BezierMaxSegments - 1;
        if (index < 1) return false;
        const auto a = path.segments[index - 1][3];
        const auto b = a + (a - path.segments[index - 1][2]);
        return addBezierSegment(path, index, a, b, c, d, tangentDelta);
    }

    PieceInterpolationSample samplePieceInterpolation(
        const PieceInterpolationPath& path, std::uint64_t tick) noexcept
    {
        PieceInterpolationSample result{};
        std::size_t used = path.used;
        if (used >= BezierMaxSegments) used = BezierMaxSegments - 1;
        if (used == 0) return result;

        float ratio = 1.0F;
        if (path.endTick != path.startTick)
        {
            const auto current = tick < path.startTick ? path.startTick : tick;
            ratio = static_cast<float>(current - path.startTick) /
                static_cast<float>(path.endTick - path.startTick);
        }
        ratio = adjustedRatio(ratio, path.speed);
        result.ratio = ratio;

        float total{};
        for (std::size_t i = 0; i < used; ++i) total += path.lengths[i];
        if (total == 0.0F) total = static_cast<float>(used);

        std::size_t segment{};
        float start{};
        float end{};
        for (; segment < used; ++segment)
        {
            end += path.lengths[segment] == 0.0F ? 1.0F : path.lengths[segment];
            if ((end / total + 0.000001F) >= ratio) break;
            start = end;
        }
        if (segment >= used) segment = used - 1;

        float localRatio = ratio;
        if (used > 1)
        {
            const float segmentLength = end - start;
            localRatio = segmentLength == 0.0F ? 1.0F :
                (ratio - start / total) / (segmentLength / total);
        }

        result.segment = segment;
        result.location = bezier(localRatio, path.segments[segment]);
        const auto nearby = bezier(
            localRatio + path.tangentDeltas[segment], path.segments[segment]);
        const auto tangent = nearby - result.location;
        if (magnitude(tangent) > 0.00001F)
            result.forward = normalized(tangent);
        result.up = {0.0F, 1.0F, 0.0F};
        return result;
    }
}
