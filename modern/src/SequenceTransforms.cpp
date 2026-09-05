#include "SequenceTransforms.hpp"

#include <cmath>
#include <type_traits>

namespace monopoly::sequence
{
    namespace
    {
        Matrix2D scale2D(float x, float y) noexcept
        { auto m = identity2D(); m.values[0] = x; m.values[4] = y; return m; }
        Matrix3D scale3D(float x, float y, float z) noexcept
        { auto m = identity3D(); m.values[0] = x; m.values[5] = y; m.values[10] = z; return m; }
        Matrix2D rotate2D(float angle) noexcept
        {
            auto m = identity2D();
            m.values[0] = m.values[4] = std::cos(angle);
            m.values[1] = std::sin(angle);
            m.values[3] = -m.values[1];
            return m;
        }
        Matrix3D rotateX(float angle) noexcept
        {
            auto m = identity3D();
            m.values[5] = m.values[10] = std::cos(angle);
            m.values[6] = std::sin(angle); m.values[9] = -m.values[6];
            return m;
        }
        Matrix3D rotateY(float angle) noexcept
        {
            auto m = identity3D();
            m.values[0] = m.values[10] = std::cos(angle);
            m.values[2] = -std::sin(angle); m.values[8] = -m.values[2];
            return m;
        }
        Matrix3D rotateZ(float angle) noexcept
        {
            auto m = identity3D();
            m.values[0] = m.values[5] = std::cos(angle);
            m.values[1] = std::sin(angle); m.values[4] = -m.values[1];
            return m;
        }
        Matrix2D osrt(const data::Sequence2DOriginScaleRotateOffsetAttribute& value) noexcept
        {
            auto result = translate2D(-value.originX, -value.originY);
            if (value.scaleX != 1.0F || value.scaleY != 1.0F)
                result = multiply(result, scale2D(value.scaleX, value.scaleY));
            if (value.rotate != 0.0F) result = multiply(result, rotate2D(value.rotate));
            if (value.offsetX != 0 || value.offsetY != 0)
                result = multiply(result, translate2D(value.offsetX, value.offsetY));
            return result;
        }
        Matrix3D osrt(const data::Sequence3DOriginScaleRotateOffsetAttribute& value) noexcept
        {
            auto result = translate3D(-value.originX, -value.originY, -value.originZ);
            if (value.scaleX != 1.0F || value.scaleY != 1.0F || value.scaleZ != 1.0F)
                result = multiply(result, scale3D(value.scaleX, value.scaleY, value.scaleZ));
            if (value.roll != 0.0F) result = multiply(result, rotateZ(value.roll));
            if (value.pitch != 0.0F) result = multiply(result, rotateX(value.pitch));
            if (value.yaw != 0.0F) result = multiply(result, rotateY(value.yaw));
            if (value.offsetX != 0.0F || value.offsetY != 0.0F || value.offsetZ != 0.0F)
                result = multiply(result, translate3D(value.offsetX, value.offsetY, value.offsetZ));
            return result;
        }
        bool is2D(const data::LegacySequenceAttribute& attribute) noexcept
        {
            return std::holds_alternative<data::Sequence2DOffsetAttribute>(attribute) ||
                std::holds_alternative<data::Sequence2DMatrixAttribute>(attribute) ||
                std::holds_alternative<data::Sequence2DOriginScaleRotateOffsetAttribute>(attribute);
        }
        bool is3D(const data::LegacySequenceAttribute& attribute) noexcept
        {
            return std::holds_alternative<data::Sequence3DOffsetAttribute>(attribute) ||
                std::holds_alternative<data::Sequence3DMatrixAttribute>(attribute) ||
                std::holds_alternative<data::Sequence3DOriginScaleRotateOffsetAttribute>(attribute);
        }
        float interpolate(float proportion, float first, float second) noexcept
        { return (1.0F - proportion) * first + proportion * second; }
        std::int32_t interpolate(float proportion, std::int32_t first,
            std::int32_t second) noexcept
        { return static_cast<std::int32_t>(interpolate(proportion,
            static_cast<float>(first), static_cast<float>(second))); }
    }

    Matrix2D identity2D() noexcept
    { Matrix2D value; value.values[0] = value.values[4] = value.values[8] = 1.0F; return value; }
    Matrix3D identity3D() noexcept
    { Matrix3D value; value.values[0] = value.values[5] = value.values[10] = value.values[15] = 1.0F; return value; }

    Matrix2D multiply(const Matrix2D& left, const Matrix2D& right) noexcept
    {
        Matrix2D result;
        for (std::size_t row = 0; row < 3; ++row)
            for (std::size_t column = 0; column < 3; ++column)
                for (std::size_t k = 0; k < 3; ++k)
                    result.values[row * 3 + column] +=
                        left.values[row * 3 + k] * right.values[k * 3 + column];
        return result;
    }
    Matrix3D multiply(const Matrix3D& left, const Matrix3D& right) noexcept
    {
        Matrix3D result;
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t column = 0; column < 4; ++column)
                for (std::size_t k = 0; k < 4; ++k)
                    result.values[row * 4 + column] +=
                        left.values[row * 4 + k] * right.values[k * 4 + column];
        return result;
    }
    Matrix2D translate2D(std::int32_t x, std::int32_t y) noexcept
    { auto value = identity2D(); value.values[6] = static_cast<float>(x); value.values[7] = static_cast<float>(y); return value; }
    Matrix3D translate3D(float x, float y, float z) noexcept
    { auto value = identity3D(); value.values[12] = x; value.values[13] = y; value.values[14] = z; return value; }

    SequenceTransform moveXYTransform(std::int32_t x, std::int32_t y) noexcept
    {
        if (x == 0 && y == 0) return std::monostate{};
        return translate2D(x, y);
    }

    Matrix3D moveRySTxzTransform(float yaw, float scale,
        float x, float z) noexcept
    {
        auto result = scale3D(scale, scale, scale);
        if (yaw != 0.0F) result = multiply(result, rotateY(yaw));
        if (x != 0.0F || z != 0.0F)
            result = multiply(result, translate3D(x, 0.0F, z));
        return result;
    }

    InitialSequenceTransform initialSequenceTransform(
        const data::LegacySequenceRecord& record,
        const data::LegacySequenceAttributes& attributes,
        std::uint8_t parentDimensionality) noexcept
    {
        std::uint8_t dimensionality{};
        if (std::holds_alternative<data::SequenceTweekerData>(record.data)) dimensionality = 0;
        else if (std::holds_alternative<data::SequenceBitmapData>(record.data)) dimensionality = 2;
        else if (std::holds_alternative<data::SequenceModelData>(record.data) ||
            std::holds_alternative<data::SequenceMeshData>(record.data)) dimensionality = 3;
        else
        {
            dimensionality = parentDimensionality;
            for (const auto& attribute : attributes.values)
            {
                if (const auto* explicitValue =
                    std::get_if<data::SequenceDimensionalityAttribute>(&attribute))
                { dimensionality = explicitValue->value; break; }
                if (is2D(attribute)) { dimensionality = 2; break; }
                if (is3D(attribute)) { dimensionality = 3; break; }
            }
        }

        SequenceTransform local = dimensionality == 2 ? SequenceTransform(identity2D()) :
            dimensionality == 3 ? SequenceTransform(identity3D()) : SequenceTransform(std::monostate{});
        bool positioned{};
        for (const auto& attribute : attributes.values)
        {
            if (dimensionality == 2)
            {
                if (const auto* value = std::get_if<data::Sequence2DOffsetAttribute>(&attribute))
                { local = translate2D(value->x, value->y); positioned = true; break; }
                if (const auto* value = std::get_if<data::Sequence2DMatrixAttribute>(&attribute))
                { local = Matrix2D{value->values}; positioned = true; break; }
                if (const auto* value = std::get_if<data::Sequence2DOriginScaleRotateOffsetAttribute>(&attribute))
                { local = osrt(*value); positioned = true; break; }
            }
            else if (dimensionality == 3)
            {
                if (const auto* value = std::get_if<data::Sequence3DOffsetAttribute>(&attribute))
                { local = translate3D(value->x, value->y, value->z); positioned = true; break; }
                if (const auto* value = std::get_if<data::Sequence3DMatrixAttribute>(&attribute))
                { local = Matrix3D{value->values}; positioned = true; break; }
                if (const auto* value = std::get_if<data::Sequence3DOriginScaleRotateOffsetAttribute>(&attribute))
                { local = osrt(*value); positioned = true; break; }
            }
        }
        return {dimensionality, std::move(local), positioned};
    }

    SequenceTransform composeSequenceWorld(const SequenceTransform& local,
        std::uint8_t dimensionality, const SequenceTransform& parentWorld,
        std::uint8_t parentDimensionality) noexcept
    {
        if (dimensionality == 2)
        {
            const auto& own = std::get<Matrix2D>(local);
            if (parentDimensionality == 2)
                return multiply(own, std::get<Matrix2D>(parentWorld));
            return own;
        }
        if (dimensionality == 3)
        {
            const auto& own = std::get<Matrix3D>(local);
            if (parentDimensionality == 3)
                return multiply(own, std::get<Matrix3D>(parentWorld));
            return own;
        }
        return std::monostate{};
    }

    std::expected<EvaluatedTweekerTransform, TweekerTransformError>
    evaluateTweekerTransform(const data::LegacySequenceAttributes& attributes,
        std::uint8_t interpolationType, std::int32_t clock,
        std::int32_t endTime, std::uint8_t parentDimensionality) noexcept
    {
        if (interpolationType > 2)
            return std::unexpected(TweekerTransformError::InvalidInterpolation);
        if (interpolationType == 0)
            return EvaluatedTweekerTransform{true, true,
                parentDimensionality == 2 ? SequenceTransform(identity2D()) :
                parentDimensionality == 3 ? SequenceTransform(identity3D()) :
                SequenceTransform(std::monostate{})};

        const data::LegacySequenceAttribute* first{};
        const data::LegacySequenceAttribute* second{};
        for (const auto& attribute : attributes.values)
        {
            if (!is2D(attribute) && !is3D(attribute)) continue;
            if (!first) first = &attribute;
            else if (attribute.index() == first->index()) { second = &attribute; break; }
        }
        if (!first) return EvaluatedTweekerTransform{};
        if ((is2D(*first) && parentDimensionality != 2) ||
            (is3D(*first) && parentDimensionality != 3))
            return std::unexpected(TweekerTransformError::DimensionalityMismatch);

        const bool linear = interpolationType == 2 && second &&
            endTime < 1'234'567'890;
        const float proportion = linear ? static_cast<float>(clock) /
            static_cast<float>(endTime) : 0.0F;
        return std::visit([&](const auto& initial)
            -> std::expected<EvaluatedTweekerTransform, TweekerTransformError>
        {
            using Attribute = std::remove_cvref_t<decltype(initial)>;
            if constexpr (std::is_same_v<Attribute, data::Sequence2DOffsetAttribute>)
            {
                auto value = initial;
                if (linear)
                {
                    const auto& final = std::get<Attribute>(*second);
                    value.x = interpolate(proportion, value.x, final.x);
                    value.y = interpolate(proportion, value.y, final.y);
                }
                return EvaluatedTweekerTransform{true, false, translate2D(value.x, value.y)};
            }
            else if constexpr (std::is_same_v<Attribute, data::Sequence2DMatrixAttribute>)
            {
                Matrix2D value{initial.values};
                if (linear)
                {
                    const auto& final = std::get<Attribute>(*second);
                    for (std::size_t i = 0; i < value.values.size(); ++i)
                        value.values[i] = interpolate(proportion, value.values[i], final.values[i]);
                }
                return EvaluatedTweekerTransform{true, false, value};
            }
            else if constexpr (std::is_same_v<Attribute, data::Sequence2DOriginScaleRotateOffsetAttribute>)
            {
                auto value = initial;
                if (linear)
                {
                    const auto& final = std::get<Attribute>(*second);
                    value.offsetX = interpolate(proportion, value.offsetX, final.offsetX);
                    value.offsetY = interpolate(proportion, value.offsetY, final.offsetY);
                    value.originX = interpolate(proportion, value.originX, final.originX);
                    value.originY = interpolate(proportion, value.originY, final.originY);
                    value.scaleX = interpolate(proportion, value.scaleX, final.scaleX);
                    value.scaleY = interpolate(proportion, value.scaleY, final.scaleY);
                    value.rotate = interpolate(proportion, value.rotate, final.rotate);
                }
                return EvaluatedTweekerTransform{true, false, osrt(value)};
            }
            else if constexpr (std::is_same_v<Attribute, data::Sequence3DOffsetAttribute>)
            {
                auto value = initial;
                if (linear)
                {
                    const auto& final = std::get<Attribute>(*second);
                    value.x = interpolate(proportion, value.x, final.x);
                    value.y = interpolate(proportion, value.y, final.y);
                    value.z = interpolate(proportion, value.z, final.z);
                }
                return EvaluatedTweekerTransform{true, false, translate3D(value.x, value.y, value.z)};
            }
            else if constexpr (std::is_same_v<Attribute, data::Sequence3DMatrixAttribute>)
            {
                Matrix3D value{initial.values};
                if (linear)
                {
                    const auto& final = std::get<Attribute>(*second);
                    for (std::size_t i = 0; i < value.values.size(); ++i)
                        value.values[i] = interpolate(proportion, value.values[i], final.values[i]);
                }
                return EvaluatedTweekerTransform{true, false, value};
            }
            else if constexpr (std::is_same_v<Attribute, data::Sequence3DOriginScaleRotateOffsetAttribute>)
            {
                auto value = initial;
                if (linear)
                {
                    const auto& final = std::get<Attribute>(*second);
                    value.offsetX = interpolate(proportion, value.offsetX, final.offsetX);
                    value.offsetY = interpolate(proportion, value.offsetY, final.offsetY);
                    value.offsetZ = interpolate(proportion, value.offsetZ, final.offsetZ);
                    value.originX = interpolate(proportion, value.originX, final.originX);
                    value.originY = interpolate(proportion, value.originY, final.originY);
                    value.originZ = interpolate(proportion, value.originZ, final.originZ);
                    value.roll = interpolate(proportion, value.roll, final.roll);
                    value.pitch = interpolate(proportion, value.pitch, final.pitch);
                    value.yaw = interpolate(proportion, value.yaw, final.yaw);
                    value.scaleX = interpolate(proportion, value.scaleX, final.scaleX);
                    value.scaleY = interpolate(proportion, value.scaleY, final.scaleY);
                    value.scaleZ = interpolate(proportion, value.scaleZ, final.scaleZ);
                }
                return EvaluatedTweekerTransform{true, false, osrt(value)};
            }
            else return EvaluatedTweekerTransform{};
        }, *first);
    }
}
