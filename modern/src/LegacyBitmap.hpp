#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace monopoly::data
{
    enum class BitmapErrorCode
    {
        None,
        FileOpenFailed,
        ReadFailed,
        HeaderTruncated,
        InvalidSignature,
        UnsupportedDibHeader,
        InvalidDimensions,
        InvalidPlanes,
        InvalidBitDepth,
        UnsupportedCompression,
        PixelDataOutOfRange,
        DeclaredSizeOutOfRange
    };


    struct BitmapError
    {
        BitmapErrorCode code{ BitmapErrorCode::None };
        std::filesystem::path path;
        std::string detail;
    };


    struct LegacyBitmapMetadata
    {
        std::uint32_t declaredFileSize{};
        std::uint32_t pixelDataOffset{};
        std::uint32_t dibHeaderSize{};
        std::int32_t width{};
        std::int32_t height{};
        std::uint16_t bitsPerPixel{};
        std::uint32_t compression{};
        std::uint32_t declaredImageSize{};

        [[nodiscard]] constexpr bool topDown() const noexcept
        {
            return height < 0;
        }
    };


    [[nodiscard]] std::string_view bitmapErrorCodeName(
        BitmapErrorCode code) noexcept;

    // Inspecteur sans SDL/GPU pour prouver les metadonnees des BMP bruts.
    // Le decodeur et l'upload runtime restent assures par SDL_LoadBMP.
    [[nodiscard]] std::expected<LegacyBitmapMetadata, BitmapError>
    inspectLegacyBitmap(std::span<const std::byte> bytes);

    [[nodiscard]] std::expected<LegacyBitmapMetadata, BitmapError>
    inspectLegacyBitmapFile(const std::filesystem::path& path);
}
