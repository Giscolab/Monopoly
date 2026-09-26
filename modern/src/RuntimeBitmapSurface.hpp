#pragma once

#include "BitmapRuntime.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
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

    struct RuntimeBitmapExtent
    {
        std::uint32_t width{};
        std::uint32_t height{};
    };

    class RuntimeBitmapStore final
    {
    public:
        [[nodiscard]] std::expected<DataId, std::string> create(
            std::uint32_t width, std::uint32_t height, bool transparent,
            std::uint8_t globalAlpha = 255);
        [[nodiscard]] std::expected<void, std::string> update(
            DataId id, LegacyBitmapRGBA8 image);
        [[nodiscard]] std::expected<void, std::string> fill(
            DataId id, std::int32_t x, std::int32_t y,
            std::int32_t width, std::int32_t height,
            std::uint32_t colorRef);
        [[nodiscard]] std::expected<void, std::string> blit(
            DataId id, const LegacyBitmapRGBA8& source,
            std::int32_t x, std::int32_t y,
            BitmapBlitMode mode = BitmapBlitMode::SourceOver);
        [[nodiscard]] std::expected<void, std::string> setGlobalAlpha(
            DataId id, std::uint8_t alpha);
        [[nodiscard]] std::optional<RuntimeBitmapExtent> extent(
            DataId id) const noexcept;
        [[nodiscard]] std::optional<std::uint8_t> globalAlpha(
            DataId id) const noexcept;
        [[nodiscard]] std::shared_ptr<const BitmapRuntimeAsset> asset(
            DataId id) const noexcept;
        [[nodiscard]] bool contains(DataId id) const noexcept
        { return static_cast<bool>(asset(id)); }
        [[nodiscard]] bool remove(DataId id) noexcept;
        void clear() noexcept;
        [[nodiscard]] std::size_t size() const noexcept { return surfaces_.size(); }

    private:
        struct Surface
        {
            LegacyBitmapRGBA8 baseImage;
            bool colourKeyTransparent{};
            std::uint8_t globalAlpha{255};
            std::shared_ptr<const BitmapRuntimeAsset> published;
        };

        void publish(DataId id, Surface& surface);

        std::uint32_t nextTag_{1};
        std::unordered_map<DataId, Surface> surfaces_;
    };
}
