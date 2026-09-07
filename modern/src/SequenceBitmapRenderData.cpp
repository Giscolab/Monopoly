#include "SequenceBitmapRenderData.hpp"
#include "RuntimeBitmapSurface.hpp"

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
            std::shared_ptr<const data::ResourceSnapshot> resources,
            const data::RuntimeBitmapStore* runtimeBitmaps)
    {
        if (!resources)
            return std::unexpected(error(
                SequenceBitmapRenderDataErrorCode::MissingResources,
                0, 0, "resource snapshot is null"));

        std::vector<SequenceBitmapRenderItem> result;
        for (const auto& instance : runtime.bitmapInstances())
        {
            if (runtimeBitmaps)
            {
                if (auto runtimeAsset = runtimeBitmaps->asset(instance.contentsDataId))
                {
                    const auto& image = runtimeAsset->image;
                    result.push_back({instance.node, instance.contentsDataId,
                        instance.priority, instance.clock, instance.worldTransform,
                        {data::LegacyDataType::Native, image.width, image.height, 0, 0, 32},
                        {}, std::move(runtimeAsset)});
                    continue;
                }
            }

            const auto metadata = resources->banks().metadata(instance.contentsDataId);
            if (!metadata)
                return std::unexpected(error(
                    SequenceBitmapRenderDataErrorCode::MetadataFailed,
                    instance.node, instance.contentsDataId, metadata.error().detail));
            if (metadata->type != data::LegacyDataType::Bitmap &&
                metadata->type != data::LegacyDataType::Uap)
                return std::unexpected(error(
                    SequenceBitmapRenderDataErrorCode::TypeMismatch,
                    instance.node, instance.contentsDataId,
                    "2D sequence content is neither LE_DATA_DataBMP nor LE_DATA_DataUAP"));

            const auto bytes = resources->banks().load(instance.contentsDataId);
            if (!bytes)
                return std::unexpected(error(
                    SequenceBitmapRenderDataErrorCode::LoadFailed,
                    instance.node, instance.contentsDataId, bytes.error().detail));

            SequenceBitmapMetadata renderMetadata{};
            renderMetadata.type = metadata->type;
            if (metadata->type == data::LegacyDataType::Bitmap)
            {
                const auto bitmap = data::inspectLegacyBitmap(**bytes);
                if (!bitmap)
                    return std::unexpected(error(
                        SequenceBitmapRenderDataErrorCode::InvalidBitmap,
                        instance.node, instance.contentsDataId, bitmap.error().detail));
                renderMetadata.width = static_cast<std::uint32_t>(bitmap->width);
                renderMetadata.height = static_cast<std::uint32_t>(
                    bitmap->topDown() ? -static_cast<std::int64_t>(bitmap->height) : bitmap->height);
                renderMetadata.bitsPerPixel = bitmap->bitsPerPixel;
            }
            else
            {
                const auto bitmap = data::inspectLegacyUap(**bytes);
                if (!bitmap)
                    return std::unexpected(error(
                        SequenceBitmapRenderDataErrorCode::InvalidBitmap,
                        instance.node, instance.contentsDataId, bitmap.error().detail));
                renderMetadata.width = bitmap->width;
                renderMetadata.height = bitmap->height;
                renderMetadata.originX = bitmap->originX;
                renderMetadata.originY = bitmap->originY;
                renderMetadata.bitsPerPixel = 8;
            }

            result.push_back({instance.node, instance.contentsDataId,
                instance.priority, instance.clock, instance.worldTransform,
                renderMetadata, *bytes});
        }
        return result;
    }
}
