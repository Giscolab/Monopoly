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
            auto asset = meshes.resolve(
                instance.contentsDataId,
                instance.rootSequenceDataId,
                instance.rootSequencePriority);
            if (!asset)
            {
                return std::unexpected(SequenceRenderDataError{
                    SequenceRenderDataErrorCode::MeshResolutionFailed,
                    instance.node, instance.contentsDataId, asset.error()});
            }
            std::shared_ptr<const data::MeshRenderData> renderData = (*asset)->renderData;
            if ((instance.meshChoice.meshIndexA != 0 ||
                 instance.meshChoice.meshIndexB != 0) &&
                (*asset)->mesh)
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
            // Static modern assets deliberately keep their base render data
            // while legacy MIMe pose selection continues to drive only HMD.
            // The glTF morph adapter will consume this same meshChoice later.
            result.push_back({instance.node, instance.contentsDataId,
                instance.rootSequenceDataId,
                instance.priority, instance.clock, instance.worldTransform,
                std::move(*asset), instance.meshChoice, instance.bounds,
                std::move(renderData), instance.rootSequencePriority});
        }
        return result;
    }
}
