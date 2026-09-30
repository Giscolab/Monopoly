#pragma once

#include "MeshRuntime.hpp"

#include <expected>
#include <filesystem>
#include <memory>

namespace monopoly::data
{
    struct ModernGltfLoadOptions
    {
        // Initial Blender-board calibration: 486 legacy units / 24 metres.
        // Kept explicit until calibrated against retail HMD board bounds.
        float unitsPerMeter{20.25F};
        // Rotation around glTF Y-up after node transforms. The recovered
        // Blender tokens are authored lengthwise on X; retail pieces use Z.
        float yawDegrees{};
        std::array<float, 3> localOffset{};
        bool groundToZero{};
        MeshRuntimeLimits limits{};
    };

    // Static GLB bridge used before PBR and morph-target support. It accepts
    // embedded GLB geometry and glTF baseColorFactor materials only.
    [[nodiscard]] std::expected<
        std::shared_ptr<const MeshRenderData>,
        MeshRuntimeError>
    loadModernGltfMesh(
        const std::filesystem::path& path,
        ModernGltfLoadOptions options = {});
}
