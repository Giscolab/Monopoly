#include "SequenceRenderData.hpp"

namespace monopoly::sequence
{
    std::expected<std::vector<SequenceMeshRenderItem>, SequenceRenderDataError>
    collectSequenceMeshRenderData(const SequenceRuntime& runtime,
        data::MeshRuntimeCache& meshes)
    {
        std::vector<SequenceMeshRenderItem> result;
        const auto instances = runtime.meshInstances();
        result.reserve(instances.size());
        for (const auto& instance : instances)
        {
            auto asset = meshes.resolve(instance.contentsDataId);
            if (!asset)
            {
                return std::unexpected(SequenceRenderDataError{
                    SequenceRenderDataErrorCode::MeshResolutionFailed,
                    instance.node, instance.contentsDataId, asset.error()});
            }
            result.push_back({instance.node, instance.contentsDataId,
                instance.priority, instance.clock, instance.worldTransform,
                std::move(*asset), instance.meshChoice});
        }
        return result;
    }
}
