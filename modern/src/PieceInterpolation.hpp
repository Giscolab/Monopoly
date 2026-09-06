#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace monopoly::pieces
{
    struct InterpolationVec3
    {
        float x{};
        float y{};
        float z{};

        friend constexpr bool operator==(const InterpolationVec3&, const InterpolationVec3&) = default;
    };

    [[nodiscard]] constexpr InterpolationVec3 operator+(
        InterpolationVec3 a, InterpolationVec3 b) noexcept
    { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    [[nodiscard]] constexpr InterpolationVec3 operator-(
        InterpolationVec3 a, InterpolationVec3 b) noexcept
    { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    [[nodiscard]] constexpr InterpolationVec3 operator*(
        InterpolationVec3 a, float s) noexcept
    { return {a.x * s, a.y * s, a.z * s}; }
    [[nodiscard]] constexpr InterpolationVec3 operator*(
        float s, InterpolationVec3 a) noexcept
    { return a * s; }

    inline constexpr std::size_t BezierMaxSegments = 10;
    inline constexpr float BezierTangentDelta = 0.0005F;

    enum class InterpolationSpeed : std::uint8_t
    {
        Linear = 1,
        AccelerateInOut = 2,
        FastAccelerateFlatMid = 3
    };

    struct PieceInterpolationPath
    {
        std::array<std::array<InterpolationVec3, 4>, BezierMaxSegments> segments{};
        std::array<float, BezierMaxSegments> lengths{};
        std::array<float, BezierMaxSegments> tangentDeltas{};
        std::size_t used{};
        std::uint64_t startTick{};
        std::uint64_t endTick{};
        InterpolationSpeed speed{InterpolationSpeed::AccelerateInOut};
    };

    struct PieceInterpolationSample
    {
        InterpolationVec3 location{};
        InterpolationVec3 forward{0.0F, 0.0F, 1.0F};
        InterpolationVec3 up{0.0F, 1.0F, 0.0F};
        float ratio{};
        std::size_t segment{};
    };

    bool addBezierSegment(PieceInterpolationPath& path, std::size_t index,
        InterpolationVec3 a, InterpolationVec3 b,
        InterpolationVec3 c, InterpolationVec3 d,
        float tangentDelta = BezierTangentDelta) noexcept;
    bool addBezierSegmentSmooth(PieceInterpolationPath& path, std::size_t index,
        InterpolationVec3 c, InterpolationVec3 d,
        float tangentDelta = BezierTangentDelta) noexcept;
    [[nodiscard]] PieceInterpolationSample samplePieceInterpolation(
        const PieceInterpolationPath& path, std::uint64_t tick) noexcept;
}
