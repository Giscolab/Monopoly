#pragma once

#include "MeshGPUResources.hpp"
#include "SequenceWorld3DSlot.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace monopoly::engine
{
    struct World3DGPUIndexedBatch
    {
        sequence::SequenceNodeId node{};
        data::DataId contentsDataId{};
        std::uint16_t priority{};
        std::int32_t clock{};
        sequence::Matrix3D worldTransform{};
        World3DRect screenBounds{};
        SDL_GPUBuffer* vertexBuffer{};
        SDL_GPUBuffer* indexBuffer{};
        SDL_GPUTexture* gpuTexture{};
        std::uint32_t firstIndex{};
        std::uint32_t indexCount{};
        data::MeshMaterial material{};
        std::optional<data::MeshTextureRegion> texture;
    };

    // Final portable scene-data boundary before the SDL_GPU graphics pipeline.
    // It preserves sequencer traversal and per-mesh batch order. Embedded HMD
    // texture pixels are already uploaded by MeshGPUCache at this boundary.
    [[nodiscard]] std::expected<std::vector<World3DGPUIndexedBatch>, MeshGPUError>
        buildWorld3DGPUScene(const SequenceWorld3DSlot& slot, MeshGPUCache& cache);
}
