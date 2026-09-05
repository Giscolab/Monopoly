#pragma once

#include "LegacySequence.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <variant>

namespace monopoly::sequence
{
    struct Matrix2D { std::array<float, 9> values{}; };
    struct Matrix3D { std::array<float, 16> values{}; };
    using SequenceTransform = std::variant<std::monostate, Matrix2D, Matrix3D>;

    struct InitialSequenceTransform
    {
        std::uint8_t dimensionality{};
        SequenceTransform local;
        bool explicitlyPositioned{};
    };

    enum class TweekerTransformError { InvalidInterpolation, DimensionalityMismatch };
    struct EvaluatedTweekerTransform
    {
        bool changed{};
        bool identity{};
        SequenceTransform transform;
    };

    [[nodiscard]] Matrix2D identity2D() noexcept;
    [[nodiscard]] Matrix3D identity3D() noexcept;
    [[nodiscard]] Matrix2D multiply(const Matrix2D& left, const Matrix2D& right) noexcept;
    [[nodiscard]] Matrix3D multiply(const Matrix3D& left, const Matrix3D& right) noexcept;
    [[nodiscard]] Matrix2D translate2D(std::int32_t x, std::int32_t y) noexcept;
    [[nodiscard]] Matrix3D translate3D(float x, float y, float z) noexcept;

    // Portable equivalents of the matrices built by the public legacy
    // MoveXY and MoveRySTxz helpers. A monostate represents MoveXY's null
    // matrix, which resets the target rather than applying a zero delta.
    [[nodiscard]] SequenceTransform moveXYTransform(
        std::int32_t x, std::int32_t y) noexcept;
    [[nodiscard]] Matrix3D moveRySTxzTransform(
        float yaw, float scale, float x, float z) noexcept;

    // StartupSequence dimensionality scan followed by the first applicable
    // positioning subchunk. Unknown/unimplemented attributes are rejected by
    // SequenceProgram before this runtime conversion is called.
    [[nodiscard]] InitialSequenceTransform initialSequenceTransform(
        const data::LegacySequenceRecord& record,
        const data::LegacySequenceAttributes& attributes,
        std::uint8_t parentDimensionality) noexcept;

    // Row-vector convention used by L_Matrix.cpp: local effects are on the
    // left and the same-dimensional parent's world matrix is on the right.
    [[nodiscard]] SequenceTransform composeSequenceWorld(
        const SequenceTransform& local, std::uint8_t dimensionality,
        const SequenceTransform& parentWorld,
        std::uint8_t parentDimensionality) noexcept;

    [[nodiscard]] std::expected<EvaluatedTweekerTransform, TweekerTransformError>
    evaluateTweekerTransform(const data::LegacySequenceAttributes& attributes,
        std::uint8_t interpolationType, std::int32_t clock,
        std::int32_t endTime, std::uint8_t parentDimensionality) noexcept;
}
