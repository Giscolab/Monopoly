#include "LegacyBitmap.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    int failures = 0;


    void expect(bool condition, std::string_view description)
    {
        if (condition)
        {
            std::cout << "[PASS] " << description << '\n';
            return;
        }

        ++failures;
        std::cerr << "[FAIL] " << description << '\n';
    }


    void writeU16Le(
        std::vector<std::byte>& bytes,
        std::size_t offset,
        std::uint16_t value)
    {
        bytes[offset] = static_cast<std::byte>(value & 0xFFU);
        bytes[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    }


    void writeU32Le(
        std::vector<std::byte>& bytes,
        std::size_t offset,
        std::uint32_t value)
    {
        bytes[offset] = static_cast<std::byte>(value & 0xFFU);
        bytes[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xFFU);
        bytes[offset + 2] = static_cast<std::byte>((value >> 16U) & 0xFFU);
        bytes[offset + 3] = static_cast<std::byte>((value >> 24U) & 0xFFU);
    }


    std::vector<std::byte> bitmapFixture()
    {
        std::vector<std::byte> bytes(58, std::byte{});
        bytes[0] = std::byte{ 'B' };
        bytes[1] = std::byte{ 'M' };
        writeU32Le(bytes, 2, static_cast<std::uint32_t>(bytes.size()));
        writeU32Le(bytes, 10, 54);
        writeU32Le(bytes, 14, 40);
        writeU32Le(bytes, 18, 1);
        writeU32Le(bytes, 22, 1);
        writeU16Le(bytes, 26, 1);
        writeU16Le(bytes, 28, 24);
        writeU32Le(bytes, 30, 0);
        writeU32Le(bytes, 34, 4);
        return bytes;
    }


    void testSyntheticMetadata()
    {
        using namespace monopoly::data;

        auto bytes = bitmapFixture();
        auto metadata = inspectLegacyBitmap(bytes);
        expect(metadata.has_value(), "minimal Windows BMP header parses");

        if (metadata)
        {
            expect(metadata->width == 1, "BMP width is little-endian");
            expect(metadata->height == 1, "BMP height is little-endian");
            expect(metadata->bitsPerPixel == 24, "BMP bit depth parsed");
            expect(metadata->compression == 0, "BI_RGB compression parsed");
            expect(!metadata->topDown(), "positive-height BMP is bottom-up");
        }

        writeU32Le(bytes, 22, 0xFFFFFFFFU);
        metadata = inspectLegacyBitmap(bytes);
        expect(
            metadata && metadata->height == -1 && metadata->topDown(),
            "negative-height BMP is recognized as top-down");
    }


    void testSyntheticFailures()
    {
        using namespace monopoly::data;

        auto bytes = bitmapFixture();
        expect(
            !inspectLegacyBitmap(
                std::span<const std::byte>(bytes).first(53)) &&
                inspectLegacyBitmap(
                    std::span<const std::byte>(bytes).first(53)).error().code ==
                    BitmapErrorCode::HeaderTruncated,
            "truncated BMP header is rejected");

        bytes = bitmapFixture();
        bytes[0] = std::byte{ 'X' };
        auto result = inspectLegacyBitmap(bytes);
        expect(
            !result && result.error().code == BitmapErrorCode::InvalidSignature,
            "non-BM signature is rejected");

        bytes = bitmapFixture();
        writeU32Le(bytes, 14, 12);
        result = inspectLegacyBitmap(bytes);
        expect(
            !result &&
                result.error().code == BitmapErrorCode::UnsupportedDibHeader,
            "pre-BITMAPINFOHEADER DIB is rejected explicitly");

        bytes = bitmapFixture();
        writeU32Le(bytes, 18, 0);
        result = inspectLegacyBitmap(bytes);
        expect(
            !result && result.error().code == BitmapErrorCode::InvalidDimensions,
            "zero width is rejected");

        bytes = bitmapFixture();
        writeU16Le(bytes, 26, 2);
        result = inspectLegacyBitmap(bytes);
        expect(
            !result && result.error().code == BitmapErrorCode::InvalidPlanes,
            "plane count other than one is rejected");

        bytes = bitmapFixture();
        writeU16Le(bytes, 28, 0);
        result = inspectLegacyBitmap(bytes);
        expect(
            !result && result.error().code == BitmapErrorCode::InvalidBitDepth,
            "zero bit depth is rejected");

        bytes = bitmapFixture();
        writeU16Le(bytes, 28, 3);
        result = inspectLegacyBitmap(bytes);
        expect(
            !result && result.error().code == BitmapErrorCode::InvalidBitDepth,
            "non-Windows BI_RGB bit depth is rejected");

        bytes = bitmapFixture();
        writeU32Le(bytes, 30, 1);
        result = inspectLegacyBitmap(bytes);
        expect(
            !result && result.error().code ==
                BitmapErrorCode::UnsupportedCompression,
            "compressed BMP data is outside the proven BI_RGB contract");

        bytes = bitmapFixture();
        writeU32Le(bytes, 10, 1000);
        result = inspectLegacyBitmap(bytes);
        expect(
            !result &&
                result.error().code == BitmapErrorCode::PixelDataOutOfRange,
            "pixel offset past EOF is rejected");

        bytes = bitmapFixture();
        writeU32Le(bytes, 2, 1000);
        result = inspectLegacyBitmap(bytes);
        expect(
            !result &&
                result.error().code == BitmapErrorCode::DeclaredSizeOutOfRange,
            "declared file size past EOF is rejected");

        bytes = bitmapFixture();
        bytes.resize(55);
        writeU32Le(bytes, 2, static_cast<std::uint32_t>(bytes.size()));
        result = inspectLegacyBitmap(bytes);
        expect(
            !result &&
                result.error().code == BitmapErrorCode::PixelDataOutOfRange,
            "scanline padding is included when rejecting a truncated raster");

        bytes = bitmapFixture();
        writeU32Le(bytes, 34, 3);
        result = inspectLegacyBitmap(bytes);
        expect(
            !result &&
                result.error().code == BitmapErrorCode::DeclaredSizeOutOfRange,
            "declared image size smaller than the padded raster is rejected");

        bytes = bitmapFixture();
        writeU32Le(bytes, 34, 8);
        result = inspectLegacyBitmap(bytes);
        expect(
            !result &&
                result.error().code == BitmapErrorCode::DeclaredSizeOutOfRange,
            "declared image range extending past EOF is rejected");

        bytes = bitmapFixture();
        writeU32Le(bytes, 2, 55);
        result = inspectLegacyBitmap(bytes);
        expect(
            !result &&
                result.error().code == BitmapErrorCode::DeclaredSizeOutOfRange,
            "declared file size smaller than the complete raster is rejected");

        bytes = bitmapFixture();
        writeU32Le(
            bytes,
            18,
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max()));
        writeU32Le(
            bytes,
            22,
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max()));
        writeU16Le(bytes, 28, 32);
        writeU32Le(bytes, 2, 0);
        writeU32Le(bytes, 34, 0);
        result = inspectLegacyBitmap(bytes);
        expect(
            !result &&
                result.error().code == BitmapErrorCode::PixelDataOutOfRange,
            "huge dimensions cannot wrap the padded-raster bounds check");
    }


#ifdef MONOPOLY_LEGACY_SOURCE_DIR
    bool isBmp(const std::filesystem::path& path)
    {
        auto extension = path.extension().string();
        std::transform(
            extension.begin(), extension.end(), extension.begin(),
            [](unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            });
        return extension == ".bmp";
    }
#endif


    void testRawTexInfoCorpus()
    {
#ifdef MONOPOLY_LEGACY_SOURCE_DIR
        using namespace monopoly::data;

        constexpr std::array<std::string_view, 4> Roots
        {{ "Boards", "Cities", "Currency", "Languages" }};
        const auto source =
            std::filesystem::path(MONOPOLY_LEGACY_SOURCE_DIR) / "monopoly";
        std::size_t total = 0;
        std::size_t indexed8 = 0;
        std::size_t rgb24 = 0;
        bool allReadable = true;
        bool allBiRgb = true;
        bool allKnownDepth = true;

        for (const auto rootName : Roots)
        {
            const auto root = source / rootName;

            for (const auto& candidate :
                std::filesystem::recursive_directory_iterator(root))
            {
                if (!candidate.is_regular_file() || !isBmp(candidate.path()))
                {
                    continue;
                }

                ++total;
                auto metadata = inspectLegacyBitmapFile(candidate.path());

                if (!metadata)
                {
                    allReadable = false;
                    std::cerr
                        << "[DETAIL] " << candidate.path().string() << ": "
                        << bitmapErrorCodeName(metadata.error().code) << ' '
                        << metadata.error().detail << '\n';
                    continue;
                }

                allBiRgb = allBiRgb && metadata->compression == 0;
                allKnownDepth = allKnownDepth &&
                    (metadata->bitsPerPixel == 8 ||
                        metadata->bitsPerPixel == 24);
                indexed8 += metadata->bitsPerPixel == 8 ? 1U : 0U;
                rgb24 += metadata->bitsPerPixel == 24 ? 1U : 0U;
            }
        }

        expect(total == 1'001, "TexInfo raw directories contain 1,001 BMPs");
        expect(allReadable, "all 1,001 raw BMP headers are structurally valid");
        expect(allBiRgb, "all raw TexInfo BMPs use BI_RGB");
        expect(allKnownDepth, "all raw TexInfo BMPs are 8-bit or 24-bit");
        expect(indexed8 + rgb24 == total, "bit-depth inventory covers every BMP");

        std::cout
            << "[INFO] TexInfo BMP depth inventory: "
            << indexed8 << " indexed 8-bit, "
            << rgb24 << " RGB 24-bit\n";
#else
        std::cout
            << "[SKIP] optional retail TexInfo BMP audit "
               "(legacy Source tree not available)\n";
#endif
    }
}


int main()
{
    testSyntheticMetadata();
    testSyntheticFailures();
    testRawTexInfoCorpus();

    if (failures != 0)
    {
        std::cerr << failures << " bitmap test(s) failed\n";
        return 1;
    }

    std::cout << "All legacy bitmap tests passed\n";
    return 0;
}
