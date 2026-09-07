#include "RuntimeBitmapSurface.hpp"

#include <algorithm>
#include <limits>

namespace monopoly::data
{
    namespace
    {
        [[nodiscard]] bool validImage(const LegacyBitmapRGBA8& image) noexcept
        {
            if (!image.width || !image.height) return false;
            const auto count = static_cast<std::uint64_t>(image.width) * image.height;
            return count <= std::numeric_limits<std::size_t>::max() / 4U &&
                image.pixels.size() == static_cast<std::size_t>(count) * 4U;
        }

        [[nodiscard]] std::uint8_t sourceOverChannel(
            std::uint8_t source, std::uint8_t destination,
            std::uint32_t sourceAlpha, std::uint32_t destinationAlpha,
            std::uint32_t outputAlpha) noexcept
        {
            if (!outputAlpha) return 0;
            const auto destinationContribution =
                (static_cast<std::uint32_t>(destination) * destinationAlpha *
                    (255U - sourceAlpha) + 127U) / 255U;
            const auto premultiplied =
                static_cast<std::uint32_t>(source) * sourceAlpha +
                destinationContribution;
            return static_cast<std::uint8_t>(std::min(
                (premultiplied + outputAlpha / 2U) / outputAlpha, 255U));
        }
    }

    bool isRuntimeBitmapDataId(DataId id) noexcept
    {
        return dataGroup(id) == RuntimeBitmapGroup && dataTag(id) != 0;
    }

    std::expected<void, std::string> blitStraightRGBA8(
        LegacyBitmapRGBA8& destination, const LegacyBitmapRGBA8& source,
        std::int32_t destinationX, std::int32_t destinationY, BitmapBlitMode mode)
    {
        if (!validImage(destination) || !validImage(source))
            return std::unexpected("invalid straight RGBA8 blit image");

        const auto left = std::max<std::int64_t>(0, destinationX);
        const auto top = std::max<std::int64_t>(0, destinationY);
        const auto right = std::min<std::int64_t>(destination.width,
            static_cast<std::int64_t>(destinationX) + source.width);
        const auto bottom = std::min<std::int64_t>(destination.height,
            static_cast<std::int64_t>(destinationY) + source.height);
        if (left >= right || top >= bottom) return {};

        for (std::int64_t y = top; y < bottom; ++y)
        {
            const auto sourceY = y - destinationY;
            for (std::int64_t x = left; x < right; ++x)
            {
                const auto sourceX = x - destinationX;
                const auto sourceOffset =
                    (static_cast<std::size_t>(sourceY) * source.width +
                        static_cast<std::size_t>(sourceX)) * 4U;
                const auto destinationOffset =
                    (static_cast<std::size_t>(y) * destination.width +
                        static_cast<std::size_t>(x)) * 4U;

                if (mode == BitmapBlitMode::Replace)
                {
                    std::copy_n(source.pixels.data() + sourceOffset, 4,
                        destination.pixels.data() + destinationOffset);
                    continue;
                }

                const auto sourceAlpha = source.pixels[sourceOffset + 3];
                if (!sourceAlpha) continue;
                const auto destinationAlpha = destination.pixels[destinationOffset + 3];
                const auto outputAlpha = sourceAlpha +
                    (static_cast<std::uint32_t>(destinationAlpha) *
                        (255U - sourceAlpha) + 127U) / 255U;
                for (std::size_t channel = 0; channel < 3; ++channel)
                    destination.pixels[destinationOffset + channel] = sourceOverChannel(
                        source.pixels[sourceOffset + channel],
                        destination.pixels[destinationOffset + channel],
                        sourceAlpha, destinationAlpha, outputAlpha);
                destination.pixels[destinationOffset + 3] =
                    static_cast<std::uint8_t>(outputAlpha);
            }
        }
        return {};
    }

    std::expected<DataId, std::string> RuntimeBitmapStore::create(
        std::uint32_t width, std::uint32_t height, bool transparent)
    {
        const auto count = static_cast<std::uint64_t>(width) * height;
        if (!width || !height || count > 16U * 1024U * 1024U ||
            count > std::numeric_limits<std::size_t>::max() / 4U)
            return std::unexpected("runtime bitmap dimensions exceed allocation budget");
        if (nextTag_ > std::numeric_limits<DataTag>::max())
            return std::unexpected("runtime bitmap DataID namespace exhausted");

        const auto id = packDataId(RuntimeBitmapGroup,
            static_cast<DataTag>(nextTag_++));
        LegacyBitmapRGBA8 image{width, height, {}};
        image.pixels.resize(static_cast<std::size_t>(count) * 4U, 0);
        if (!transparent)
            for (std::size_t offset = 3; offset < image.pixels.size(); offset += 4)
                image.pixels[offset] = 255;
        auto asset = std::make_shared<const BitmapRuntimeAsset>(
            BitmapRuntimeAsset{id, LegacyDataType::Native, {}, std::move(image)});
        assets_.emplace(id, std::move(asset));
        return id;
    }

    std::expected<void, std::string> RuntimeBitmapStore::update(
        DataId id, LegacyBitmapRGBA8 image)
    {
        const auto found = assets_.find(id);
        if (found == assets_.end())
            return std::unexpected("runtime bitmap DataID is not allocated");
        if (!validImage(image) || image.width != found->second->image.width ||
            image.height != found->second->image.height)
            return std::unexpected("runtime bitmap replacement extent mismatch");
        found->second = std::make_shared<const BitmapRuntimeAsset>(
            BitmapRuntimeAsset{id, LegacyDataType::Native, {}, std::move(image)});
        return {};
    }

    std::shared_ptr<const BitmapRuntimeAsset> RuntimeBitmapStore::asset(
        DataId id) const noexcept
    {
        const auto found = assets_.find(id);
        return found == assets_.end() ? nullptr : found->second;
    }

    bool RuntimeBitmapStore::remove(DataId id) noexcept
    {
        return assets_.erase(id) != 0;
    }

    void RuntimeBitmapStore::clear() noexcept
    {
        assets_.clear();
    }
}
