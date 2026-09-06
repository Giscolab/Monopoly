#include "SequenceBitmapRenderData.hpp"

namespace monopoly::sequence
{
    namespace
    {
        SequenceBitmapRenderDataError error(
            SequenceBitmapRenderDataErrorCode code,
            SequenceNodeId node,
            data::DataId id,
            std::string detail)
        {
            return {code, node, id, std::move(detail)};
        }
    }

    std::expected<std::vector<SequenceBitmapRenderItem>,
        SequenceBitmapRenderDataError> collectSequenceBitmapRenderData(
            const SequenceRuntime& runtime,
            std::shared_ptr<const data::ResourceSnapshot> resources)
    {
        if (!resources)
            return std::unexpected(error(
                SequenceBitmapRenderDataErrorCode::MissingResources,
                0, 0, "resource snapshot is null"));

        std::vector<SequenceBitmapRenderItem> result;
        for (const auto& instance : runtime.bitmapInstances())
        {
            const auto metadata = resources->banks().metadata(instance.contentsDataId);
            if (!metadata)
                return std::unexpected(error(
                    SequenceBitmapRenderDataErrorCode::MetadataFailed,
                    instance.node, instance.contentsDataId, metadata.error().detail));
            if (metadata->type != data::LegacyDataType::Bitmap)
                return std::unexpected(error(
                    SequenceBitmapRenderDataErrorCode::TypeMismatch,
                    instance.node, instance.contentsDataId,
                    "2D sequence content is not LE_DATA_DataBMP"));

            const auto bytes = resources->banks().load(instance.contentsDataId);
            if (!bytes)
                return std::unexpected(error(
                    SequenceBitmapRenderDataErrorCode::LoadFailed,
                    instance.node, instance.contentsDataId, bytes.error().detail));
            const auto bitmap = data::inspectLegacyBitmap(**bytes);
            if (!bitmap)
                return std::unexpected(error(
                    SequenceBitmapRenderDataErrorCode::InvalidBitmap,
                    instance.node, instance.contentsDataId, bitmap.error().detail));
            result.push_back({instance.node, instance.contentsDataId,
                instance.priority, instance.clock, instance.worldTransform,
                *bitmap, *bytes});
        }
        return result;
    }
}
