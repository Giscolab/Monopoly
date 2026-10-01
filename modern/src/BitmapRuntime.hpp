#pragma once
#include "LegacyBitmap.hpp"
#include "LegacyDataArchive.hpp"
#include <memory>
#include <array>
#include <optional>
#include <unordered_map>

namespace monopoly::data
{
    struct BitmapRuntimeAsset
    {
        DataId dataId{};
        LegacyDataType sourceType{LegacyDataType::Unknown};
        SharedDataBytes source;
        LegacyBitmapRGBA8 image;
        // Opt-in presentation artwork. Retail and runtime text retain nearest sampling.
        bool preferLinearFiltering{};
        // Qualified presentation-only rectangle; original CNK bounds remain untouched.
        std::optional<std::array<float, 4>> presentationRect;
    };
    // Immutable payload identity prevents DataId reuse across snapshots from
    // returning stale pixels. Consumers retain their old asset after replacement.
    class BitmapRuntimeCache final
    {
    public:
        [[nodiscard]] std::expected<std::shared_ptr<const BitmapRuntimeAsset>, BitmapError>
            resolve(DataId id, LegacyDataType sourceType, SharedDataBytes bytes);
        // Called after render publication retires the previous consumers.
        // Keep active immutable assets; unused entries must not pin DAT leases.
        [[nodiscard]] std::size_t releaseUnused() noexcept;
        void clear() noexcept { assets_.clear(); }
        [[nodiscard]] std::size_t size() const noexcept { return assets_.size(); }
    private:
        std::unordered_map<DataId, std::shared_ptr<const BitmapRuntimeAsset>> assets_;
    };
}
