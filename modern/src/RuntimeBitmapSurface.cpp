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

    void RuntimeBitmapStore::publish(DataId id, Surface& surface)
    {
        auto image = surface.baseImage;

        for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4U)
        {
            auto& alpha = image.pixels[offset + 3U];

            if (!surface.colourKeyTransparent)
            {
                alpha = 255;
                continue;
            }

            const bool pureGreen =
                image.pixels[offset] == 0U &&
                image.pixels[offset + 1U] == 255U &&
                image.pixels[offset + 2U] == 0U;
            if (pureGreen)
            {
                alpha = 0;
                continue;
            }

            alpha = static_cast<std::uint8_t>(
                static_cast<std::uint32_t>(alpha) * surface.globalAlpha / 255U);
        }

        surface.published = std::make_shared<const BitmapRuntimeAsset>(
            BitmapRuntimeAsset{id, LegacyDataType::Native, {}, std::move(image)});
    }

    std::expected<DataId, std::string> RuntimeBitmapStore::create(
        std::uint32_t width, std::uint32_t height, bool transparent,
        std::uint8_t globalAlpha)
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
        image.pixels.resize(static_cast<std::size_t>(count) * 4U);

        for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4U)
        {
            image.pixels[offset] = 0;
            image.pixels[offset + 1U] = transparent ? 255 : 0;
            image.pixels[offset + 2U] = 0;
            image.pixels[offset + 3U] = transparent ? 0 : 255;
        }

        Surface surface{std::move(image), transparent, globalAlpha, {}};
        publish(id, surface);
        surfaces_.emplace(id, std::move(surface));
        return id;
    }

    std::expected<void, std::string> RuntimeBitmapStore::update(
        DataId id, LegacyBitmapRGBA8 image)
    {
        const auto found = surfaces_.find(id);
        if (found == surfaces_.end())
            return std::unexpected("runtime bitmap DataID is not allocated");
        if (!validImage(image) ||
            image.width != found->second.baseImage.width ||
            image.height != found->second.baseImage.height)
            return std::unexpected("runtime bitmap replacement extent mismatch");

        found->second.baseImage = std::move(image);
        publish(id, found->second);
        return {};
    }

    std::expected<void, std::string> RuntimeBitmapStore::fill(
        DataId id, std::int32_t x, std::int32_t y,
        std::int32_t width, std::int32_t height,
        std::uint32_t colorRef)
    {
        const auto found = surfaces_.find(id);
        if (found == surfaces_.end())
            return std::unexpected("runtime bitmap DataID is not allocated");
        if (width <= 0 || height <= 0)
            return {};

        auto image = found->second.baseImage;
        const auto left = std::max<std::int64_t>(0, x);
        const auto top = std::max<std::int64_t>(0, y);
        const auto right = std::min<std::int64_t>(
            image.width, static_cast<std::int64_t>(x) + width);
        const auto bottom = std::min<std::int64_t>(
            image.height, static_cast<std::int64_t>(y) + height);
        if (left >= right || top >= bottom)
            return {};

        const auto red = static_cast<std::uint8_t>(colorRef & 0xFFU);
        const auto green = static_cast<std::uint8_t>((colorRef >> 8U) & 0xFFU);
        const auto blue = static_cast<std::uint8_t>((colorRef >> 16U) & 0xFFU);
        const bool keyed = found->second.colourKeyTransparent &&
            red == 0U && green == 255U && blue == 0U;
        const auto alpha = static_cast<std::uint8_t>(keyed ? 0U : 255U);

        for (std::int64_t row = top; row < bottom; ++row)
        {
            for (std::int64_t column = left; column < right; ++column)
            {
                const auto offset =
                    (static_cast<std::size_t>(row) * image.width +
                        static_cast<std::size_t>(column)) * 4U;
                image.pixels[offset] = red;
                image.pixels[offset + 1U] = green;
                image.pixels[offset + 2U] = blue;
                image.pixels[offset + 3U] = alpha;
            }
        }

        found->second.baseImage = std::move(image);
        publish(id, found->second);
        return {};
    }

    std::expected<void, std::string> RuntimeBitmapStore::blit(
        DataId id, const LegacyBitmapRGBA8& source,
        std::int32_t x, std::int32_t y, BitmapBlitMode mode)
    {
        const auto found = surfaces_.find(id);
        if (found == surfaces_.end())
            return std::unexpected("runtime bitmap DataID is not allocated");

        auto image = found->second.baseImage;
        const auto result = blitStraightRGBA8(image, source, x, y, mode);
        if (!result)
            return result;

        found->second.baseImage = std::move(image);
        publish(id, found->second);
        return {};
    }

    std::expected<void, std::string> RuntimeBitmapStore::setGlobalAlpha(
        DataId id, std::uint8_t alpha)
    {
        const auto found = surfaces_.find(id);
        if (found == surfaces_.end())
            return std::unexpected("runtime bitmap DataID is not allocated");

        found->second.globalAlpha = alpha;
        publish(id, found->second);
        return {};
    }

    std::optional<RuntimeBitmapExtent> RuntimeBitmapStore::extent(
        DataId id) const noexcept
    {
        const auto found = surfaces_.find(id);
        if (found == surfaces_.end())
            return std::nullopt;
        return RuntimeBitmapExtent{
            found->second.baseImage.width, found->second.baseImage.height};
    }

    std::optional<std::uint8_t> RuntimeBitmapStore::globalAlpha(
        DataId id) const noexcept
    {
        const auto found = surfaces_.find(id);
        if (found == surfaces_.end())
            return std::nullopt;
        return found->second.globalAlpha;
    }

    std::shared_ptr<const BitmapRuntimeAsset> RuntimeBitmapStore::asset(
        DataId id) const noexcept
    {
        const auto found = surfaces_.find(id);
        return found == surfaces_.end() ? nullptr : found->second.published;
    }

    bool RuntimeBitmapStore::remove(DataId id) noexcept
    {
        return surfaces_.erase(id) != 0;
    }

    void RuntimeBitmapStore::clear() noexcept
    {
        surfaces_.clear();
    }
}
