#pragma once

#include <array>
#include <cstddef>

namespace monopoly::engine
{
    // Shared ABI for World3D HLSL/DXIL/MSL, used by both the 3D renderer
    // and the unlit 2D texture pipeline. Keep stage layouts in one place.
    struct alignas(16) World3DVertexUniforms
    {
        std::array<float, 16> worldViewProjection{};
        std::array<float, 16> world{};
        std::array<float, 4> materialDiffuse{};
        std::array<float, 4> sceneAmbient{};
        std::array<float, 4> boardReflectionColorEnabled{};
        std::array<float, 4> boardReflectionDirection{};
        std::array<float, 4> sunColorEnabled{};
        std::array<float, 4> sunDirection{};
        std::array<float, 4> spotlightColorEnabled{};
        std::array<float, 4> spotlightPositionRange{};
        std::array<float, 4> spotlightDirectionFalloff{};
        std::array<float, 4> spotlightAttenuationTheta{};
        std::array<float, 4> spotlightPhi{};
    };

    struct alignas(16) World3DFragmentUniforms
    {
        std::array<float, 4> materialDiffuse{};
    };
    static_assert(sizeof(World3DVertexUniforms) == 304U);
    static_assert(sizeof(World3DFragmentUniforms) == 16U);

    static_assert(offsetof(World3DVertexUniforms, world) == 64U);
    static_assert(offsetof(World3DVertexUniforms, materialDiffuse) == 128U);
    static_assert(offsetof(World3DVertexUniforms, sceneAmbient) == 144U);
    static_assert(offsetof(World3DVertexUniforms, spotlightPhi) == 288U);
}
