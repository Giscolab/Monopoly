#pragma once

#include <array>
#include <cstddef>

namespace monopoly::engine
{
    struct alignas(16) ModernPBRVertexUniforms
    {
        std::array<float, 16> worldViewProjection{};
        std::array<float, 16> world{};
    };

    struct alignas(16) ModernPBRFragmentUniforms
    {
        std::array<float, 4> baseColor{};
        // x: metallic, y: roughness, z: output target already encodes sRGB.
        std::array<float, 4> metallicRoughness{};
        std::array<float, 4> emissiveStrength{};
        std::array<float, 4> cameraPosition{};
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
    static_assert(sizeof(ModernPBRVertexUniforms) == 128U);
    static_assert(sizeof(ModernPBRFragmentUniforms) == 224U);
    static_assert(offsetof(ModernPBRFragmentUniforms, spotlightPhi) == 208U);
}
