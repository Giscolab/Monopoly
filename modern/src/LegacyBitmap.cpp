#include "LegacyBitmap.hpp"

#include <fstream>
#include <limits>
#include <utility>
#include <vector>

namespace monopoly::data
{
    namespace
    {
        [[nodiscard]] std::uint16_t readU16Le(
            const std::byte* bytes) noexcept
        {
            return
                static_cast<std::uint16_t>(
                    std::to_integer<std::uint8_t>(bytes[0])) |
                static_cast<std::uint16_t>(
                    std::to_integer<std::uint8_t>(bytes[1]) << 8U);
        }


        [[nodiscard]] std::uint32_t readU32Le(
            const std::byte* bytes) noexcept
        {
            return
                static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[0])) |
                (static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[1])) << 8U) |
                (static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[2])) << 16U) |
                (static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[3])) << 24U);
        }


        [[nodiscard]] BitmapError error(
            BitmapErrorCode code,
            std::string detail,
            std::filesystem::path path = {})
        {
            return { code, std::move(path), std::move(detail) };
        }


        [[nodiscard]] constexpr bool isSupportedBiRgbBitDepth(
            std::uint16_t bitsPerPixel) noexcept
        {
            switch (bitsPerPixel)
            {
            case 1:
            case 4:
            case 8:
            case 16:
            case 24:
            case 32:
                return true;

            default:
                return false;
            }
        }


        [[nodiscard]] constexpr bool checkedAdd(
            std::uint64_t left,
            std::uint64_t right,
            std::uint64_t& result) noexcept
        {
            if (left > std::numeric_limits<std::uint64_t>::max() - right)
            {
                return false;
            }

            result = left + right;
            return true;
        }


        [[nodiscard]] constexpr bool checkedMultiply(
            std::uint64_t left,
            std::uint64_t right,
            std::uint64_t& result) noexcept
        {
            if (left != 0 &&
                right > std::numeric_limits<std::uint64_t>::max() / left)
            {
                return false;
            }

            result = left * right;
            return true;
        }
    }


    std::string_view bitmapErrorCodeName(BitmapErrorCode code) noexcept
    {
        switch (code)
        {
        case BitmapErrorCode::None: return "None";
        case BitmapErrorCode::FileOpenFailed: return "FileOpenFailed";
        case BitmapErrorCode::ReadFailed: return "ReadFailed";
        case BitmapErrorCode::HeaderTruncated: return "HeaderTruncated";
        case BitmapErrorCode::InvalidSignature: return "InvalidSignature";
        case BitmapErrorCode::UnsupportedDibHeader:
            return "UnsupportedDibHeader";
        case BitmapErrorCode::InvalidDimensions: return "InvalidDimensions";
        case BitmapErrorCode::InvalidPlanes: return "InvalidPlanes";
        case BitmapErrorCode::InvalidBitDepth: return "InvalidBitDepth";
        case BitmapErrorCode::UnsupportedCompression:
            return "UnsupportedCompression";
        case BitmapErrorCode::PixelDataOutOfRange:
            return "PixelDataOutOfRange";
        case BitmapErrorCode::DeclaredSizeOutOfRange:
            return "DeclaredSizeOutOfRange";
        }

        return "InvalidBitmapErrorCode";
    }


    std::expected<LegacyBitmapMetadata, BitmapError> inspectLegacyBitmap(
        std::span<const std::byte> bytes)
    {
        // BITMAPFILEHEADER (14) + les 40 octets communs de BITMAPINFOHEADER.
        constexpr std::size_t MinimumHeaderSize = 54;

        if (bytes.size() < MinimumHeaderSize)
        {
            return std::unexpected(error(
                BitmapErrorCode::HeaderTruncated,
                "BMP requires a 14-byte file header and 40-byte DIB header"));
        }

        if (std::to_integer<std::uint8_t>(bytes[0]) != 'B' ||
            std::to_integer<std::uint8_t>(bytes[1]) != 'M')
        {
            return std::unexpected(error(
                BitmapErrorCode::InvalidSignature,
                "legacy asset is not a Windows BM bitmap"));
        }

        LegacyBitmapMetadata metadata
        {
            readU32Le(bytes.data() + 2),
            readU32Le(bytes.data() + 10),
            readU32Le(bytes.data() + 14),
            static_cast<std::int32_t>(readU32Le(bytes.data() + 18)),
            static_cast<std::int32_t>(readU32Le(bytes.data() + 22)),
            readU16Le(bytes.data() + 28),
            readU32Le(bytes.data() + 30),
            readU32Le(bytes.data() + 34)
        };

        if (metadata.dibHeaderSize < 40 ||
            static_cast<std::uint64_t>(14) + metadata.dibHeaderSize >
                bytes.size())
        {
            return std::unexpected(error(
                BitmapErrorCode::UnsupportedDibHeader,
                "BMP DIB header must expose at least BITMAPINFOHEADER"));
        }

        if (metadata.width <= 0 || metadata.height == 0 ||
            metadata.height == std::numeric_limits<std::int32_t>::min())
        {
            return std::unexpected(error(
                BitmapErrorCode::InvalidDimensions,
                "BMP width must be positive and height must be non-zero"));
        }

        if (readU16Le(bytes.data() + 26) != 1)
        {
            return std::unexpected(error(
                BitmapErrorCode::InvalidPlanes,
                "Windows BMP plane count must equal one"));
        }

        if (!isSupportedBiRgbBitDepth(metadata.bitsPerPixel))
        {
            return std::unexpected(error(
                BitmapErrorCode::InvalidBitDepth,
                "BI_RGB BMP bit depth must be 1, 4, 8, 16, 24 or 32"));
        }

        constexpr std::uint32_t BiRgb = 0;

        if (metadata.compression != BiRgb)
        {
            return std::unexpected(error(
                BitmapErrorCode::UnsupportedCompression,
                "legacy texture inspection supports only BI_RGB BMP data"));
        }

        const std::uint64_t dibEnd =
            14U + static_cast<std::uint64_t>(metadata.dibHeaderSize);

        if (metadata.pixelDataOffset < dibEnd ||
            static_cast<std::uint64_t>(metadata.pixelDataOffset) >=
                bytes.size())
        {
            return std::unexpected(error(
                BitmapErrorCode::PixelDataOutOfRange,
                "BMP pixel data offset is outside the file"));
        }

        const auto absoluteHeight = metadata.height < 0
            ? static_cast<std::uint64_t>(-
                static_cast<std::int64_t>(metadata.height))
            : static_cast<std::uint64_t>(metadata.height);
        const auto width = static_cast<std::uint64_t>(metadata.width);
        std::uint64_t rowBits{};
        std::uint64_t paddedRowBits{};
        std::uint64_t rowStride{};
        std::uint64_t minimumRasterSize{};
        std::uint64_t minimumRasterEnd{};

        // Chaque scanline BI_RGB est arrondie au DWORD suivant. Les calculs
        // restent explicites sur 64 bits afin qu'un header hostile ne puisse
        // pas ramener une taille immense dans la plage du fichier.
        if (!checkedMultiply(
                width,
                static_cast<std::uint64_t>(metadata.bitsPerPixel),
                rowBits) ||
            !checkedAdd(rowBits, 31U, paddedRowBits) ||
            !checkedMultiply(paddedRowBits / 32U, 4U, rowStride) ||
            !checkedMultiply(
                rowStride,
                absoluteHeight,
                minimumRasterSize) ||
            !checkedAdd(
                metadata.pixelDataOffset,
                minimumRasterSize,
                minimumRasterEnd))
        {
            return std::unexpected(error(
                BitmapErrorCode::PixelDataOutOfRange,
                "BMP raster size overflows portable 64-bit bounds"));
        }

        if (minimumRasterEnd > static_cast<std::uint64_t>(bytes.size()))
        {
            return std::unexpected(error(
                BitmapErrorCode::PixelDataOutOfRange,
                "BMP pixel array is truncated for its dimensions and bit depth"));
        }

        if (metadata.declaredImageSize != 0)
        {
            std::uint64_t declaredImageEnd{};

            if (metadata.declaredImageSize < minimumRasterSize ||
                !checkedAdd(
                    metadata.pixelDataOffset,
                    metadata.declaredImageSize,
                    declaredImageEnd) ||
                declaredImageEnd > static_cast<std::uint64_t>(bytes.size()))
            {
                return std::unexpected(error(
                    BitmapErrorCode::DeclaredSizeOutOfRange,
                    "BMP declared image size does not contain a complete raster"));
            }
        }

        if (metadata.declaredFileSize != 0 &&
            (metadata.declaredFileSize > bytes.size() ||
                metadata.declaredFileSize < minimumRasterEnd ||
                (metadata.declaredImageSize != 0 &&
                    static_cast<std::uint64_t>(metadata.declaredFileSize) <
                        static_cast<std::uint64_t>(
                            metadata.pixelDataOffset) +
                            metadata.declaredImageSize)))
        {
            return std::unexpected(error(
                BitmapErrorCode::DeclaredSizeOutOfRange,
                "BMP declared file size does not contain its declared raster"));
        }

        return metadata;
    }


    std::expected<LegacyBitmapMetadata, BitmapError>
    inspectLegacyBitmapFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);

        if (!input)
        {
            return std::unexpected(error(
                BitmapErrorCode::FileOpenFailed,
                "unable to open legacy BMP",
                path));
        }

        const auto end = input.tellg();

        if (end < 0 || static_cast<std::uint64_t>(end) >
            std::numeric_limits<std::size_t>::max())
        {
            return std::unexpected(error(
                BitmapErrorCode::ReadFailed,
                "unable to determine a portable BMP size",
                path));
        }

        std::vector<std::byte> bytes(static_cast<std::size_t>(end));
        input.seekg(0, std::ios::beg);

        if (!bytes.empty())
        {
            input.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        }

        if (!input)
        {
            return std::unexpected(error(
                BitmapErrorCode::ReadFailed,
                "unable to read complete legacy BMP",
                path));
        }

        auto metadata = inspectLegacyBitmap(bytes);

        if (!metadata)
        {
            auto failure = metadata.error();
            failure.path = path;
            return std::unexpected(std::move(failure));
        }

        return metadata;
    }
}
