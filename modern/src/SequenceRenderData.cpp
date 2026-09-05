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
            std::shared_ptr<const data::MeshRenderData> renderData = (*asset)->renderData;
            if (instance.meshChoice.meshIndexA != 0 || instance.meshChoice.meshIndexB != 0)
            {
                auto evaluated = data::makeMeshRenderData(*(*asset)->mesh,
                    instance.meshChoice.meshIndexA, instance.meshChoice.meshIndexB,
                    instance.meshChoice.meshProportion);
                if (!evaluated)
                {
                    return std::unexpected(SequenceRenderDataError{
                        SequenceRenderDataErrorCode::MeshPoseEvaluationFailed,
                        instance.node, instance.contentsDataId, evaluated.error()});
                }
                renderData = std::make_shared<const data::MeshRenderData>(
                    std::move(*evaluated));
            }
            result.push_back({instance.node, instance.contentsDataId,
                instance.priority, instance.clock, instance.worldTransform,
                std::move(*asset), instance.meshChoice, std::move(renderData)});
        }
        return result;
    }
}
