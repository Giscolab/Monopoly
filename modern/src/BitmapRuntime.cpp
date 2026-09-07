#include "BitmapRuntime.hpp"
namespace monopoly::data
{
    std::expected<std::shared_ptr<const BitmapRuntimeAsset>, BitmapError>
    BitmapRuntimeCache::resolve(
        DataId id, LegacyDataType sourceType, SharedDataBytes bytes)
    {
        if (!bytes)
            return std::unexpected(BitmapError{BitmapErrorCode::ReadFailed, {},
                "missing immutable bitmap payload"});
        const auto found = assets_.find(id);
        if (found != assets_.end() && found->second->source == bytes &&
            found->second->sourceType == sourceType)
            return found->second;

        std::expected<LegacyBitmapRGBA8, BitmapError> decoded =
            std::unexpected(BitmapError{BitmapErrorCode::UnsupportedDataType, {},
                "bitmap runtime supports only DataBMP and DataUAP"});
        if (sourceType == LegacyDataType::Bitmap)
            decoded = decodeLegacyBitmapRGBA8(*bytes);
        else if (sourceType == LegacyDataType::Uap)
            decoded = decodeLegacyUapRGBA8(*bytes);
        if (!decoded) return std::unexpected(decoded.error());

        auto asset = std::make_shared<const BitmapRuntimeAsset>(
            BitmapRuntimeAsset{id, sourceType, std::move(bytes), std::move(*decoded)});
        assets_.insert_or_assign(id, asset);
        return asset;
    }
}
