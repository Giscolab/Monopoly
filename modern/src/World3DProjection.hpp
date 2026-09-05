#pragma once

#include "MeshRuntime.hpp"
#include "SequenceTransforms.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>

namespace monopoly::engine
{
    struct World3DRect
    {
        std::int32_t left{};
        std::int32_t top{};
        std::int32_t right{};
        std::int32_t bottom{};

        [[nodiscard]] bool empty() const noexcept
        { return left >= right || top >= bottom; }
        auto operator<=>(const World3DRect&) const = default;
    };

    struct World3DCamera
    {
        std::array<float, 3> location{0.0F, 0.0F, -320.0F};
        std::array<float, 3> forward{0.0F, 0.0F, 1.0F};
        std::array<float, 3> up{0.0F, 1.0F, 0.0F};
        float fieldOfView{0.7853981633974483F};
        float nearPlane{1.0F};
        float farPlane{5000.0F};
        auto operator<=>(const World3DCamera&) const = default;
    };

    inline constexpr float MonopolyBoardNearPlane = 10.0F;
    inline constexpr float MonopolyBoardFarPlane = 1540.0F;

    enum class World3DProjectionErrorCode
    {
        InvalidViewport,
        InvalidPlanes,
        InvalidFieldOfView,
        InvalidCameraDirection
    };

    struct World3DProjectionError
    {
        World3DProjectionErrorCode code{};
    };

    struct World3DProjectionState
    {
        World3DRect viewport{};
        World3DCamera camera{};
        sequence::Matrix3D view{};
        sequence::Matrix3D projection{};
        // Portable raster equivalent of D3DVIEWPORT2 clip-aspect handling.
        // The historical projection matrix itself stays untouched above.
        sequence::Matrix3D rasterProjection{};
        sequence::Matrix3D viewportMatrix{};
        sequence::Matrix3D cameraToScreen{};
    };

    [[nodiscard]] std::expected<World3DProjectionState, World3DProjectionError>
    makeWorld3DProjectionState(World3DRect viewport, World3DCamera camera = {});

    [[nodiscard]] std::optional<World3DRect> world3DMeshScreenRect(
        const data::MeshBounds& bounds,
        const sequence::Matrix3D& worldTransform,
        const World3DProjectionState& projection) noexcept;

    [[nodiscard]] std::optional<World3DRect> intersectWorld3DRect(
        World3DRect left, World3DRect right) noexcept;
}
