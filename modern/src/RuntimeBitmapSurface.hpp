#pragma once

#include "BitmapRuntime.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <unordered_map>

namespace monopoly::data
{
    inline constexpr std::uint16_t RuntimeBitmapGroup = 0xFFFEU;

    enum class BitmapBlitMode
    {
        Replace,
        SourceOver
    };

    [[nodiscard]] bool isRuntimeBitmapDataId(DataId id) noexcept;

    [[nodiscard]] std::expected<void, std::string> blitStraightRGBA8(
        LegacyBitmapRGBA8& destination,
        const LegacyBitmapRGBA8& source,
        std::int32_t destinationX,
        std::int32_t destinationY,
        BitmapBlitMode mode);

    class RuntimeBitmapStore final
    {
    public:
        [[nodiscard]] std::expected<DataId, std::string> create(
            std::uint32_t width, std::uint32_t height, bool transparent);
        [[nodiscard]] std::expected<void, std::string> update(
            DataId id, LegacyBitmapRGBA8 image);
        [[nodiscard]] std::shared_ptr<const BitmapRuntimeAsset> asset(
            DataId id) const noexcept;
        [[nodiscard]] bool contains(DataId id) const noexcept
        { return static_cast<bool>(asset(id)); }
        [[nodiscard]] bool remove(DataId id) noexcept;
        void clear() noexcept;
        [[nodiscard]] std::size_t size() const noexcept { return assets_.size(); }

    private:
        std::uint32_t nextTag_{1};
        std::unordered_map<DataId, std::shared_ptr<const BitmapRuntimeAsset>> assets_;
    };
}
