#pragma once
#include "LegacyBitmap.hpp"
#include "LegacyDataArchive.hpp"
#include <memory>
#include <unordered_map>

namespace monopoly::data
{
    struct BitmapRuntimeAsset
    {
        DataId dataId{};
        LegacyDataType sourceType{LegacyDataType::Unknown};
        SharedDataBytes source;
        LegacyBitmapRGBA8 image;
    };
    // Immutable payload identity prevents DataId reuse across snapshots from
    // returning stale pixels. Consumers retain their old asset after replacement.
    class BitmapRuntimeCache final
    {
    public:
        [[nodiscard]] std::expected<std::shared_ptr<const BitmapRuntimeAsset>, BitmapError>
            resolve(DataId id, LegacyDataType sourceType, SharedDataBytes bytes);
        void clear() noexcept { assets_.clear(); }
        [[nodiscard]] std::size_t size() const noexcept { return assets_.size(); }
    private:
        std::unordered_map<DataId, std::shared_ptr<const BitmapRuntimeAsset>> assets_;
    };
}
