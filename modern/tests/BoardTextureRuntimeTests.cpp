#include "BoardTextureRuntime.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    using namespace monopoly::data;
    int failures{};
    void expect(bool condition, std::string_view description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << description << '\n';
        if (!condition) ++failures;
    }
    struct Fixture
    {
        std::filesystem::path root = std::filesystem::current_path() /
            ("BoardTextureRuntime-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        Fixture() { std::filesystem::create_directories(root); }
        ~Fixture()
        {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }
    };
    void put(std::vector<std::uint8_t>& bytes, std::size_t at,
        std::uint32_t value, unsigned count)
    {
        for (unsigned i = 0; i < count; ++i)
            bytes[at + i] = static_cast<std::uint8_t>(value >> (i * 8U));
    }
    std::vector<std::uint8_t> bmp(std::uint32_t width, bool overlay,
        bool topDown = false, bool indexed = true)
    {
        const std::uint32_t offset = 54U + (indexed ? 256U * 4U : 0U);
        const std::uint32_t stride = (width * (indexed ? 1U : 3U) + 3U) & ~3U;
        std::vector<std::uint8_t> bytes(offset + stride * width);
        bytes[0] = 'B'; bytes[1] = 'M';
        put(bytes, 2, static_cast<std::uint32_t>(bytes.size()), 4);
        put(bytes, 10, offset, 4); put(bytes, 14, 40, 4);
        put(bytes, 18, width, 4);
        put(bytes, 22, topDown ? 0U - width : width, 4);
        put(bytes, 26, 1, 2); put(bytes, 28, indexed ? 8 : 24, 2);
        put(bytes, 34, stride * width, 4);
        if (indexed)
        {
            put(bytes, 46, 256, 4);
            // Both palette entries 0 and 1 are red: only index 0 is transparent.
            for (std::size_t index = 0; index < 2; ++index)
            {
                bytes[54 + index * 4] = 7;
                bytes[55 + index * 4] = 9;
                bytes[56 + index * 4] = 201;
            }
            bytes[54 + 2 * 4 + (overlay ? 0 : 1)] = 255;
        }
        for (std::uint32_t y = 0; y < width; ++y)
        {
            const auto storageY = topDown ? y : width - 1U - y;
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const auto at = offset + storageY * stride + x * (indexed ? 1U : 3U);
                if (indexed)
                    bytes[at] = overlay ? (y == 0 && x == 1 ? 1 :
                        y == width - 1 && x == 0 ? 2 : 0) : 2;
                else
                {
                    bytes[at] = 3; bytes[at + 1] = 5; bytes[at + 2] = 11;
                }
            }
        }
        return bytes;
    }
    bool write(const Fixture& fixture, std::string_view relative,
        const std::vector<std::uint8_t>& bytes)
    {
        const auto path = fixture.root / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        return output.good();
    }
    BoardTextureRecipe recipe(bool withOverlay)
    {
        BoardTextureRecipe result;
        result.mesh = BoardMeshKind::ClassicMedium;
        result.meshDataId = boardMeshDataId(result.mesh);
        result.resolution = TextureResolution::Pixels128;
        result.provision = TextureProvision::ExternalSubstitutions;
        TextureReference reference;
        reference.coordinate = {352, 384};
        reference.location = TextureLocation::Language;
        reference.role = TextureRole::Language;
        reference.fileName = "FixtureBase.BMP";
        if (withOverlay)
            reference.overlay = TextureOverlayReference{
                TextureRole::BoardOverlay, TextureLocation::SelectedBoard, "FixtureOverlay.BMP"};
        result.textures.push_back(reference);
        return result;
    }
    bool pixel(const HmdTextureImage& image, std::uint32_t x, std::uint32_t y,
        std::array<std::uint8_t, 4> expected)
    {
        const auto at = (static_cast<std::size_t>(y) * image.width + x) * 4U;
        return at + 4 <= image.rgba.size() && image.rgba[at] == expected[0] &&
            image.rgba[at + 1] == expected[1] && image.rgba[at + 2] == expected[2] &&
            image.rgba[at + 3] == expected[3];
    }
    void testContextPaths()
    {
        const BoardTextureContext europe{BoardEdition::Europe, LanguageId::French, 11, 12};
        const auto language = boardTextureRelativePath(BoardMeshKind::ClassicMedium,
            TextureLocation::Language, "LANG01_128.BMP", europe);
        const auto board = boardTextureRelativePath(BoardMeshKind::ClassicMedium,
            TextureLocation::SelectedBoard, "BRD01_128.BMP", europe);
        const auto currency = boardTextureRelativePath(BoardMeshKind::ClassicHigh,
            TextureLocation::Currency, "GO_256.BMP", europe);
        expect(language && *language == "Languages/Lang01/Medium/LANG01_128.BMP" &&
            board && *board == "Boards/Board11/Medium/BRD01_128.BMP" &&
            currency && *currency == "Currency/Curr12/High/GO_256.BMP",
            "European paths derive independently from language, board, currency and mesh detail");
        BoardTextureContext usa;
        usa.city = 10;
        const auto classic = boardTextureRelativePath(BoardMeshKind::ClassicHigh,
            TextureLocation::SelectedCityNames, "CT01_256.BMP", usa);
        const auto city = boardTextureRelativePath(BoardMeshKind::CityMedium,
            TextureLocation::SelectedCityPhotos, "CT01_128.BMP", usa);
        expect(classic && *classic == "Cities/City00/High/CT01_256.BMP" &&
            city && *city == "Cities/City10/Photos/CT01_128.BMP",
            "classic city names stay in City00 while city photos use selected city");
        usa.city = 11;
        expect(!boardTextureRelativePath(BoardMeshKind::CityMedium,
            TextureLocation::SelectedCityPhotos, "CT01_128.BMP", usa) &&
            !boardTextureRelativePath(BoardMeshKind::ClassicMedium,
                TextureLocation::Language, "../outside.bmp", europe) &&
            !boardTextureRelativePath(BoardMeshKind::CityMedium,
                TextureLocation::Language, "LANG01_128.BMP", europe),
            "invalid stock city, traversal filename and unsupported European city mesh fail");
    }
    void testCatalogDimensionException()
    {
        Fixture fixture;
        const auto paths = ResourcePaths::create(std::array{fixture.root});
        expect(paths.has_value(), "city texture fixture roots initialize");
        if (!paths) return;
        auto cityRecipe = recipe(false);
        cityRecipe.mesh = BoardMeshKind::CityHigh;
        cityRecipe.meshDataId = boardMeshDataId(cityRecipe.mesh);
        cityRecipe.textures.front().location = TextureLocation::SelectedCityNames;
        cityRecipe.textures.front().fileName = "CT07_256.BMP";
        BoardTextureContext context;
        context.city = 1;
        expect(write(fixture, "Cities/City01/High/CT07_256.BMP", bmp(256, false, false, false)),
            "known high-detail city-name bitmap is written at its physical 256px dimensions");
        const auto images = loadBoardTextureImages(*paths, cityRecipe, context);
        expect(images && images->size() == 1 && images->front()->width == 256 &&
            images->front()->height == 256 && pixel(*images->front(), 0, 0, {11, 5, 3, 255}),
            "128px recipe preserves the catalog's intentional 256px city-name asset and RGB pixels");
    }
    void testPaletteOverlayAndFailure()
    {
        Fixture fixture;
        const auto paths = ResourcePaths::create(std::array{fixture.root});
        expect(paths.has_value(), "board texture fixture resource roots initialize");
        if (!paths) return;
        const BoardTextureContext context{BoardEdition::Europe, LanguageId::French, 11, 12};
        constexpr std::string_view basePath = "Languages/Lang01/Medium/FixtureBase.BMP";
        constexpr std::string_view overlayPath = "Boards/Board11/Medium/FixtureOverlay.BMP";
        expect(write(fixture, basePath, bmp(128, false)), "indexed base bitmap fixture written");
        for (const bool topDown : {false, true})
        {
            expect(write(fixture, overlayPath, bmp(128, true, topDown)),
                "overlay fixture written with explicit row orientation");
            const auto loaded = loadBoardTextureImages(*paths, recipe(true), context);
            expect(loaded && loaded->size() == 1,
                "complete board texture recipe loads without a renderer");
            if (!loaded || loaded->empty()) continue;
            const auto& image = *loaded->front();
            expect(image.rawX == 352 && image.rawY == 384 && image.texturePage == 149 &&
                image.logicalX == 64 && image.logicalY == 128 &&
                image.width == 128 && image.height == 128,
                "loaded images preserve raw coordinates and historical page/local mapping");
            expect(pixel(image, 0, 0, {0, 255, 0, 255}) &&
                pixel(image, 1, 0, {201, 9, 7, 255}) &&
                pixel(image, 0, 127, {0, 0, 255, 255}),
                "only palette index zero is transparent in bottom-up and top-down overlays");
        }
        auto incomplete = recipe(true);
        incomplete.textures.push_back(incomplete.textures.front());
        incomplete.textures.back().fileName = "Missing.BMP";
        expect(!loadBoardTextureImages(*paths, incomplete, context),
            "a later missing asset rejects the complete batch instead of returning partial images");
        expect(write(fixture, overlayPath, bmp(128, true, false, false)),
            "nonpalettized overlay fixture written");
        expect(!loadBoardTextureImages(*paths, recipe(true), context),
            "unsupported nonpalettized overlay fails without invented transparency");
        expect(write(fixture, basePath, bmp(64, false)), "wrong-size base fixture written");
        expect(!loadBoardTextureImages(*paths, recipe(false), context),
            "BMP dimensions must match the recipe or known catalog dimensions");
        auto truncated = bmp(128, false);
        truncated.resize(20);
        expect(write(fixture, basePath, truncated), "truncated bitmap fixture written");
        expect(!loadBoardTextureImages(*paths, recipe(false), context),
            "truncated BMP fails through the production decoder");
        auto embedded = recipe(true);
        embedded.provision = TextureProvision::EmbeddedInMesh;
        auto invalidEmbedded = embedded;
        invalidEmbedded.meshDataId = EmptyDataId;
        expect(!loadBoardTextureImages(*paths, invalidEmbedded, context),
            "embedded provision still rejects a malformed recipe identity");
        auto invalidContext = context;
        invalidContext.city = -1;
        const auto bypass = loadBoardTextureImages(*paths, embedded, invalidContext);
        expect(bypass && bypass->empty(),
            "embedded board recipe needs no external texture files or context lookup");
    }
}
int main()
{
    testContextPaths();
    testPaletteOverlayAndFailure();
    testCatalogDimensionException();
    std::cout << (failures ? "Board texture runtime tests FAILED\n" :
        "Board texture runtime tests passed\n");
    return failures ? 1 : 0;
}
