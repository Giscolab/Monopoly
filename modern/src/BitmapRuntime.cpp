#include "BitmapRuntime.hpp"
namespace monopoly::data
{
    std::expected<std::shared_ptr<const BitmapRuntimeAsset>, BitmapError>
    BitmapRuntimeCache::resolve(DataId id, SharedDataBytes bytes)
    {
        if (!bytes)
            return std::unexpected(BitmapError{BitmapErrorCode::ReadFailed, {},
                "missing immutable BMP payload"});
        const auto found = assets_.find(id);
        if (found != assets_.end() && found->second->source == bytes)
            return found->second;
        auto decoded = decodeLegacyBitmapRGBA8(*bytes);
        if (!decoded) return std::unexpected(decoded.error());
        auto asset = std::make_shared<const BitmapRuntimeAsset>(
            BitmapRuntimeAsset{id, std::move(bytes), std::move(*decoded)});
        assets_.insert_or_assign(id, asset);
        return asset;
    }
}
