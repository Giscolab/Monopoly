#pragma once

#include "MeshRuntime.hpp"

#include <expected>

namespace monopoly::data
{
    struct MeshRefinementOptions
    {
        unsigned levels{1};
        bool smoothControlVertices{true};
        bool modernMetalMaterial{true};
        float metallic{0.92F};
        float roughness{0.24F};
    };

    // Deterministic Loop-style triangle refinement. Each input triangle becomes
    // four triangles per level. Edge vertices are shared globally, batches keep
    // their original material ownership, and normals/bounds are rebuilt.
    [[nodiscard]] std::expected<MeshRenderData, MeshRuntimeError>
    refineTriangleMesh(const MeshRenderData& source,
        MeshRefinementOptions options = {});

    // Production preset for classic 1999 token geometry: one Loop smoothing
    // pass followed by one topology-only split. This preserves the recognisable
    // HMD silhouette while replacing 1999-era faceting with dense PBR geometry.
    [[nodiscard]] std::expected<MeshRenderData, MeshRuntimeError>
    refineClassicTokenMesh(const MeshRenderData& source);
}
