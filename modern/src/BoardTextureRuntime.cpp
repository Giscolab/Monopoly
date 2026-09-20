#include "BoardTextureRuntime.hpp"

#include "LegacyBitmap.hpp"

#include <algorithm>
#include <fstream>
#include <utility>

namespace monopoly::data
{
    namespace
    {
        struct LoadedTexture
        {
            std::vector<std::byte> bytes;
            LegacyBitmapMetadata metadata;
            LegacyBitmapRGBA8 image;
        };

        bool sameAssetPath(std::string_view left, std::string_view right)
        {
            const auto fold = [](char c)
            {
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 'a' - 'A') : c;
            };
            return left.size() == right.size() && std::equal(
                left.begin(), left.end(), right.begin(),
                [fold](char a, char b) { return fold(a) == fold(b); });
        }

        std::expected<LoadedTexture, std::string> loadTexture(
            const ResourcePaths& paths, const BoardTextureRecipe& recipe,
            const BoardTextureContext& context, TextureLocation location,
            std::string_view fileName)
        {
            const auto relative = boardTextureRelativePath(
                recipe.mesh, location, fileName, context);
            if (!relative)
                return std::unexpected(std::string(relative.error().detail));
            const auto resolved = paths.resolve(*relative);
            if (!resolved)
                return std::unexpected(*relative + ": " + resolved.error().detail);

            // Stock TexInfo images are at most 256x256 BI_RGB 24-bit.
            // Bound file allocation as well as the existing decoder's pixels.
            constexpr std::uint64_t MaximumFileBytes = 1024U * 1024U;
            std::ifstream input(*resolved, std::ios::binary | std::ios::ate);
            if (!input)
                return std::unexpected(*relative + ": cannot open texture BMP");
            const auto size = input.tellg();
            if (size < 0 || static_cast<std::uint64_t>(size) > MaximumFileBytes)
                return std::unexpected(*relative + ": texture BMP exceeds file budget");
            LoadedTexture result;
            result.bytes.resize(static_cast<std::size_t>(size));
            input.seekg(0, std::ios::beg);
            if (!result.bytes.empty())
                input.read(reinterpret_cast<char*>(result.bytes.data()),
                    static_cast<std::streamsize>(result.bytes.size()));
            if (!input)
                return std::unexpected(*relative + ": cannot read complete texture BMP");
            const auto metadata = inspectLegacyBitmap(result.bytes);
            if (!metadata)
                return std::unexpected(*relative + ": " + metadata.error().detail);
            auto image = decodeLegacyBitmapRGBA8(result.bytes, 256U * 256U);
            if (!image)
                return std::unexpected(*relative + ": " + image.error().detail);

            auto expected = textureDimensions(recipe.resolution);
            // TexInfo's high-detail 128 recipe intentionally references three
            // 256 city-name files. Preserve their physical corpus dimensions.
            for (const auto& asset : legacyTextureCorpusManifest())
            {
                if (sameAssetPath(asset.relativePath, *relative))
                {
                    expected = asset.expectedDimensions;
                    break;
                }
            }
            if (image->width != expected.width || image->height != expected.height)
                return std::unexpected(*relative + ": texture BMP dimensions do not match the catalog");
            result.metadata = *metadata;
            result.image = std::move(*image);
            return result;
        }
    }

    std::expected<std::vector<std::shared_ptr<const HmdTextureImage>>, std::string>
    loadBoardTextureImages(
        const ResourcePaths& paths,
        const BoardTextureRecipe& recipe,
        const BoardTextureContext& context)
    {
        std::vector<std::shared_ptr<const HmdTextureImage>> images;
        if ((recipe.provision != TextureProvision::EmbeddedInMesh &&
             recipe.provision != TextureProvision::ExternalSubstitutions) ||
            !isValidBoardMeshKind(recipe.mesh) ||
            !isValidTextureResolution(recipe.resolution) ||
            recipe.meshDataId != boardMeshDataId(recipe.mesh))
            return std::unexpected("invalid board texture recipe");
        if (recipe.provision == TextureProvision::EmbeddedInMesh)
            return images;
        images.reserve(recipe.textures.size());
        for (const auto& reference : recipe.textures)
        {
            auto base = loadTexture(paths, recipe, context,
                reference.location, reference.fileName);
            if (!base)
                return std::unexpected(base.error());
            if (reference.overlay)
            {
                auto overlay = loadTexture(paths, recipe, context,
                    reference.overlay->location, reference.overlay->fileName);
                if (!overlay)
                    return std::unexpected(overlay.error());
                if (base->metadata.bitsPerPixel != 8 || overlay->metadata.bitsPerPixel != 8)
                    return std::unexpected("nonpalettized board overlays are not supported");
                if (overlay->image.width != base->image.width ||
                    overlay->image.height != base->image.height)
                    return std::unexpected("board overlay dimensions differ from the base texture");

                // UDUtils.cpp uses source palette index zero as DDCOLORKEY,
                // not an RGB color. Read indices only after decoder validation;
                // equal colors stored at nonzero indices must remain opaque.
                const auto width = overlay->image.width;
                const auto height = overlay->image.height;
                const std::size_t stride = (static_cast<std::size_t>(width) + 3U) & ~std::size_t{3};
                for (std::uint32_t y = 0; y < height; ++y)
                {
                    const auto sourceY = overlay->metadata.topDown() ? y : height - 1U - y;
                    const auto* row = overlay->bytes.data() + overlay->metadata.pixelDataOffset + sourceY * stride;
                    for (std::uint32_t x = 0; x < width; ++x)
                    {
                        if (row[x] == std::byte{0}) continue;
                        const auto pixel = (static_cast<std::size_t>(y) * width + x) * 4U;
                        std::copy_n(overlay->image.pixels.data() + pixel, 4,
                            base->image.pixels.data() + pixel);
                    }
                }
            }
            auto image = std::make_shared<HmdTextureImage>();
            const auto x = reference.coordinate.x;
            const auto y = reference.coordinate.y;
            image->texturePage = static_cast<std::uint16_t>(
                ((y & 0x200U) << 2U) | ((y & 0x100U) >> 4U) |
                ((x & 0x3FFU) >> 6U) | (1U << 7U));
            image->rawX = x;
            image->rawY = y;
            image->logicalX = static_cast<std::int32_t>((x & 63U) * 2U);
            image->logicalY = static_cast<std::int32_t>(y & 255U);
            image->width = base->image.width;
            image->height = base->image.height;
            image->rgba = std::move(base->image.pixels);
            images.push_back(std::move(image));
        }
        return images;
    }
}
