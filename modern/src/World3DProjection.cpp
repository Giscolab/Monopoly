#include "World3DProjection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace monopoly::engine
{
    namespace
    {
        struct Point3D { float x{}, y{}, z{}; };

        float dot(const std::array<float, 3>& a,
            const std::array<float, 3>& b) noexcept
        { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

        std::array<float, 3> subtract(const std::array<float, 3>& a,
            const std::array<float, 3>& b) noexcept
        { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }

        std::array<float, 3> scale(const std::array<float, 3>& a, float value) noexcept
        { return {a[0] * value, a[1] * value, a[2] * value}; }

        float magnitude(const std::array<float, 3>& value) noexcept
        { return std::sqrt(dot(value, value)); }

        std::array<float, 3> normalize(const std::array<float, 3>& value) noexcept
        {
            const auto length = magnitude(value);
            return {value[0] / length, value[1] / length, value[2] / length};
        }

        std::array<float, 3> cross(const std::array<float, 3>& a,
            const std::array<float, 3>& b) noexcept
        {
            return {
                a[1] * b[2] - a[2] * b[1],
                a[2] * b[0] - a[0] * b[2],
                a[0] * b[1] - a[1] * b[0]};
        }

        Point3D transformPoint(Point3D point,
            const sequence::Matrix3D& matrix) noexcept
        {
            const auto& m = matrix.values;
            const float x = m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12];
            const float y = m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13];
            const float z = m[2] * point.x + m[6] * point.y + m[10] * point.z + m[14];
            const float w = m[3] * point.x + m[7] * point.y + m[11] * point.z + m[15];
            if (w == 0.0F) return {};
            return {x / w, y / w, z / w};
        }

        std::optional<std::int32_t> coord(float value) noexcept
        {
            if (!std::isfinite(value)) return std::nullopt;
            constexpr auto low = static_cast<float>(std::numeric_limits<std::int32_t>::min());
            constexpr auto high = static_cast<float>(std::numeric_limits<std::int32_t>::max());
            if (value < low || value > high) return std::nullopt;
            return static_cast<std::int32_t>(value);
        }

        sequence::Matrix3D makeView(const World3DCamera& camera) noexcept
        {
            const auto forward = normalize(camera.forward);
            const auto projected = scale(forward, dot(camera.up, forward));
            const auto up = normalize(subtract(camera.up, projected));
            const auto right = cross(up, forward);
            auto result = sequence::identity3D();
            result.values[0] = right[0]; result.values[1] = up[0]; result.values[2] = forward[0];
            result.values[4] = right[1]; result.values[5] = up[1]; result.values[6] = forward[1];
            result.values[8] = right[2]; result.values[9] = up[2]; result.values[10] = forward[2];
            result.values[12] = -dot(camera.location, right);
            result.values[13] = -dot(camera.location, up);
            result.values[14] = -dot(camera.location, forward);
            return result;
        }

        sequence::Matrix3D makeProjection(const World3DCamera& camera) noexcept
        {
            sequence::Matrix3D result{};
            const float cotangent = std::cos(camera.fieldOfView * 0.5F) /
                std::sin(camera.fieldOfView * 0.5F);
            result.values[0] = cotangent;
            result.values[5] = cotangent;
            result.values[10] = camera.farPlane / (camera.farPlane - camera.nearPlane);
            result.values[11] = 1.0F;
            result.values[14] = -result.values[10] * camera.nearPlane;
            return result;
        }

        sequence::Matrix3D makeViewport(World3DRect viewport) noexcept
        {
            auto result = sequence::identity3D();
            const auto width = viewport.right - viewport.left;
            const auto height = viewport.bottom - viewport.top;
            const float aspect = static_cast<float>(width) / static_cast<float>(height);
            result.values[0] = static_cast<float>(width / 2);
            result.values[5] = aspect * static_cast<float>((viewport.top - viewport.bottom) / 2);
            result.values[12] = static_cast<float>(viewport.left + width / 2);
            result.values[13] = static_cast<float>(viewport.top + height / 2);
            return result;
        }

        bool finiteVector(const std::array<float, 3>& value) noexcept
        { return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]); }
    }

    std::expected<World3DProjectionState, World3DProjectionError>
    makeWorld3DProjectionState(World3DRect viewport, World3DCamera camera)
    {
        if (viewport.empty())
            return std::unexpected(World3DProjectionError{
                World3DProjectionErrorCode::InvalidViewport});
        if (!std::isfinite(camera.nearPlane) || !std::isfinite(camera.farPlane) ||
            camera.nearPlane <= 0.0F || camera.farPlane <= camera.nearPlane)
            return std::unexpected(World3DProjectionError{
                World3DProjectionErrorCode::InvalidPlanes});
        if (!std::isfinite(camera.fieldOfView) || camera.fieldOfView <= 0.0F ||
            camera.fieldOfView >= 3.14159265358979323846F)
            return std::unexpected(World3DProjectionError{
                World3DProjectionErrorCode::InvalidFieldOfView});
        if (!finiteVector(camera.location) || !finiteVector(camera.forward) ||
            !finiteVector(camera.up) || magnitude(camera.forward) <= 0.000001F)
            return std::unexpected(World3DProjectionError{
                World3DProjectionErrorCode::InvalidCameraDirection});

        const auto forward = normalize(camera.forward);
        const auto projectedUp = subtract(camera.up, scale(forward, dot(camera.up, forward)));
        if (magnitude(projectedUp) <= 0.000001F)
            return std::unexpected(World3DProjectionError{
                World3DProjectionErrorCode::InvalidCameraDirection});

        World3DProjectionState result;
        result.viewport = viewport;
        result.camera = camera;
        result.view = makeView(camera);
        result.projection = makeProjection(camera);
        result.viewportMatrix = makeViewport(viewport);
        result.cameraToScreen = sequence::multiply(result.projection, result.viewportMatrix);
        return result;
    }

    std::optional<World3DRect> intersectWorld3DRect(
        World3DRect left, World3DRect right) noexcept
    {
        World3DRect result{
            std::max(left.left, right.left),
            std::max(left.top, right.top),
            std::min(left.right, right.right),
            std::min(left.bottom, right.bottom)};
        return result.empty() ? std::nullopt : std::optional<World3DRect>{result};
    }

    std::optional<World3DRect> world3DMeshScreenRect(
        const data::MeshBounds& bounds,
        const sequence::Matrix3D& worldTransform,
        const World3DProjectionState& projection) noexcept
    {
        const auto modelToCamera = sequence::multiply(worldTransform, projection.view);
        std::array<Point3D, 8> corners{};
        for (std::size_t i = 0; i < corners.size(); ++i)
        {
            const Point3D point{
                (i & 4U) ? bounds.maximum[0] : bounds.minimum[0],
                (i & 2U) ? bounds.maximum[1] : bounds.minimum[1],
                (i & 1U) ? bounds.maximum[2] : bounds.minimum[2]};
            corners[i] = transformPoint(point, modelToCamera);
        }

        std::vector<Point3D> visible;
        visible.reserve(16);
        for (const auto& point : corners)
            if (point.z >= projection.camera.nearPlane)
                visible.push_back(point);
        if (visible.empty()) return std::nullopt;

        if (visible.size() < corners.size())
        {
            for (std::size_t i = 0; i < corners.size(); ++i)
            {
                for (std::size_t dimension = 0; dimension < 3; ++dimension)
                {
                    const auto j = i ^ (std::size_t{1} << dimension);
                    if (j < i) continue;
                    const bool firstVisible = corners[i].z >= projection.camera.nearPlane;
                    const bool secondVisible = corners[j].z >= projection.camera.nearPlane;
                    if (firstVisible == secondVisible) continue;
                    const float denominator = corners[j].z - corners[i].z;
                    if (denominator == 0.0F) continue;
                    const float proportion =
                        (projection.camera.nearPlane - corners[i].z) / denominator;
                    visible.push_back({
                        corners[i].x + (corners[j].x - corners[i].x) * proportion,
                        corners[i].y + (corners[j].y - corners[i].y) * proportion,
                        projection.camera.nearPlane});
                }
            }
        }

        std::vector<Point3D> screen;
        screen.reserve(visible.size());
        for (const auto& point : visible)
            screen.push_back(transformPoint(point, projection.cameraToScreen));
        if (screen.empty()) return std::nullopt;

        auto firstX = coord(screen.front().x);
        auto firstY = coord(screen.front().y);
        if (!firstX || !firstY) return std::nullopt;
        World3DRect rectangle{*firstX, *firstY, *firstX, *firstY};
        for (const auto& point : screen)
        {
            auto x = coord(point.x);
            auto y = coord(point.y);
            if (!x || !y) return std::nullopt;
            rectangle.left = std::min(rectangle.left, *x);
            rectangle.right = std::max(rectangle.right, *x);
            rectangle.top = std::min(rectangle.top, *y);
            rectangle.bottom = std::max(rectangle.bottom, *y);
        }

        if (rectangle.left > std::numeric_limits<std::int32_t>::min()) --rectangle.left;
        if (rectangle.top > std::numeric_limits<std::int32_t>::min()) --rectangle.top;
        if (rectangle.right < std::numeric_limits<std::int32_t>::max()) ++rectangle.right;
        if (rectangle.bottom < std::numeric_limits<std::int32_t>::max()) ++rectangle.bottom;
        return intersectWorld3DRect(rectangle, projection.viewport);
    }
}
