#include "World3DGPUScene.hpp"

#include <limits>

namespace monopoly::engine
{
    std::expected<std::vector<World3DGPUIndexedBatch>, MeshGPUError>
    buildWorld3DGPUScene(const SequenceWorld3DSlot& slot, MeshGPUCache& cache)
    {
        std::vector<World3DGPUIndexedBatch> result;
        for (const auto node : slot.visibleOrder())
        {
            const auto* object = slot.find(node);
            if (object == nullptr || !object->screenBounds || !object->asset ||
                !object->asset->renderData)
                continue;

            auto gpu = cache.resolve(object->asset);
            if (!gpu) return std::unexpected(gpu.error());
            const auto& renderData = *object->asset->renderData;
            result.reserve(result.size() + renderData.batches.size());

            for (const auto& batch : renderData.batches)
            {
                if (batch.firstIndex > std::numeric_limits<std::uint32_t>::max() ||
                    batch.indexCount > std::numeric_limits<std::uint32_t>::max())
                    return std::unexpected(MeshGPUError{
                        MeshGPUErrorCode::SizeOverflow,
                        "GPU draw batch exceeds 32-bit indexed draw range"});
                result.push_back({
                    object->node,
                    object->contentsDataId,
                    object->priority,
                    object->clock,
                    object->worldTransform,
                    *object->screenBounds,
                    (*gpu)->vertexBuffer,
                    (*gpu)->indexBuffer,
                    static_cast<std::uint32_t>(batch.firstIndex),
                    static_cast<std::uint32_t>(batch.indexCount),
                    batch.material,
                    batch.texture});
            }
        }
        return result;
    }
}
