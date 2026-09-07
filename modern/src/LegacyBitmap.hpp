#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
        DeclaredSizeOutOfRange,
        InvalidPalette,
        DecodeBudgetExceeded,
        UnsupportedDataType
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

    struct LegacyBitmapRGBA8
    {
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<std::uint8_t> pixels; // top-down, straight RGBA8
    };

    struct LegacyUapMetadata
    {
        std::uint16_t width{};
        std::uint16_t height{};
        std::int16_t originX{};
        std::int16_t originY{};
        std::uint32_t flags{};
        std::uint16_t colourCount{};
        std::uint16_t alphaCount{};
        std::size_t pixelDataOffset{};
        std::size_t rowStride{};
    };

    // NEWBITMAPHEADER / DataUAP contract from L_Type.h. UAP raster is
    // top-down and each 8-bit row is DWORD padded.
    [[nodiscard]] std::expected<LegacyUapMetadata, BitmapError>
    inspectLegacyUap(std::span<const std::byte> bytes);

    [[nodiscard]] std::expected<LegacyBitmapRGBA8, BitmapError>
    decodeLegacyUapRGBA8(std::span<const std::byte> bytes,
        std::size_t maxPixels = 16U * 1024U * 1024U);

    // BI_RGB 8/24 only. BMP reserved palette bytes are not alpha.
    // L_Data.cpp:7490 sets BITMAP_NOTRANSPARENCY for DataBMP.
    [[nodiscard]] std::expected<LegacyBitmapRGBA8, BitmapError>
    decodeLegacyBitmapRGBA8(std::span<const std::byte> bytes,
        std::size_t maxPixels = 16U * 1024U * 1024U);

    // Inspecteur sans SDL/GPU pour prouver les metadonnees des BMP bruts.
    [[nodiscard]] std::expected<LegacyBitmapMetadata, BitmapError>
    inspectLegacyBitmap(std::span<const std::byte> bytes);

    [[nodiscard]] std::expected<LegacyBitmapMetadata, BitmapError>
    inspectLegacyBitmapFile(const std::filesystem::path& path);
}
