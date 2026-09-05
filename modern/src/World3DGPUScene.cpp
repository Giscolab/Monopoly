#include "World3DGPUScene.hpp"

#include <limits>

namespace monopoly::engine
{
    std::expected<std::vector<World3DGPUIndexedBatch>, MeshGPUError>
    buildWorld3DGPUScene(const SequenceWorld3DSlot& slot, MeshGPUCache& cache)
    {
        std::vector<World3DGPUIndexedBatch> result;
        std::vector<std::uint64_t> activeDynamicVertices;
        for (const auto node : slot.visibleOrder())
        {
            const auto* object = slot.find(node);
            if (object == nullptr || !object->screenBounds || !object->asset ||
                !object->asset->renderData)
                continue;

            const auto renderData = object->renderData ?
                object->renderData : object->asset->renderData;
            if (!renderData) continue;
            auto gpu = cache.resolve(object->asset);
            if (!gpu) return std::unexpected(gpu.error());
            SDL_GPUBuffer* vertexBuffer = (*gpu)->vertexBuffer;
            if (renderData != object->asset->renderData)
            {
                auto dynamic = cache.resolveDynamicVertices(object->node,
                    object->asset, renderData);
                if (!dynamic) return std::unexpected(dynamic.error());
                vertexBuffer = (*dynamic)->vertexBuffer;
                activeDynamicVertices.push_back(object->node);
            }
            result.reserve(result.size() + renderData->batches.size());

            for (const auto& batch : renderData->batches)
            {
                if (batch.firstIndex > std::numeric_limits<std::uint32_t>::max() ||
                    batch.indexCount > std::numeric_limits<std::uint32_t>::max())
                    return std::unexpected(MeshGPUError{
                        MeshGPUErrorCode::SizeOverflow,
                        "GPU draw batch exceeds 32-bit indexed draw range"});

                SDL_GPUTexture* gpuTexture = nullptr;
                if (batch.texture)
                {
                    gpuTexture = (*gpu)->texture(batch.texture->key);
                    if (gpuTexture == nullptr)
                        return std::unexpected(MeshGPUError{
                            MeshGPUErrorCode::MissingTexturePixels,
                            "textured HMD batch has no uploaded SDL_GPU texture"});
                }

                result.push_back({
                    object->node,
                    object->contentsDataId,
                    object->priority,
                    object->clock,
                    object->worldTransform,
                    *object->screenBounds,
                    vertexBuffer,
                    (*gpu)->indexBuffer,
                    gpuTexture,
                    static_cast<std::uint32_t>(batch.firstIndex),
                    static_cast<std::uint32_t>(batch.indexCount),
                    batch.material,
                    batch.texture});
            }
        }
        cache.pruneDynamicVertices(activeDynamicVertices);
        return result;
    }
}
