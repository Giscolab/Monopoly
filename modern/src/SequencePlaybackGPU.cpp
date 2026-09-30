#include "SequencePlayback.hpp"
#include "MeshGPUResources.hpp"
#include "ModernEnvironment.hpp"

#include <algorithm>

#include <SDL3/SDL_log.h>

namespace monopoly::engine
{
    std::expected<void, std::string> SequencePlayback::prepareModernMeshes(MeshGPUCache& cache,
        const std::function<void(const data::MeshRenderData*)>& rejectPack)
    {
        bool rejected{};
        const auto instances = runtime_.meshInstances();
        for (const auto node : world_.order())
        {
            const auto* object = world_.find(node);
            if (!object || !object->asset ||
                object->asset->origin != data::MeshAssetOrigin::ModernGltf)
                continue;
            const auto uploaded = cache.resolveForScene(object->asset);
            if (uploaded) continue;
            if (rejectPack) rejectPack(object->asset->renderData.get());
            if (data::dataGroup(object->contentsDataId) == ModernEnvironmentLogicalGroup)
            {
                const auto* failed = object->asset->renderData.get();
                std::erase_if(nativeSceneItems_, [&](const auto& item)
                    { return item.asset && item.asset->renderData.get() == failed; });
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Optional native decoration upload rejected: %s; decoration disabled",
                    uploaded.error().detail.c_str());
                rejected = true;
                continue;
            }
            if (meshes_.rejectModernAsset(object->asset->renderData.get()))
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Modern GLB DATA 0x%08x GPU upload rejected: %s; using complete retail sequence",
                    static_cast<unsigned>(object->contentsDataId), uploaded.error().detail.c_str());
            for (const auto& instance : instances)
                if (instance.node == node)
                    meshes_.rejectModernSequence(instance.rootSequenceDataId);
            rejected = true;
        }
        if (rejected) return publishRuntimeViews();
        return {};
    }
}
