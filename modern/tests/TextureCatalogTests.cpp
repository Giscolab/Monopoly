#include "TextureCatalog.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>

namespace
{
    int failures = 0;


    void expect(
        bool condition,
        std::string_view description)
    {
        if (condition)
        {
            std::cout << "[PASS] " << description << '\n';
            return;
        }

        ++failures;
        std::cerr << "[FAIL] " << description << '\n';
    }


    template <std::size_t Size>
    [[nodiscard]] bool matches(
        std::span<const monopoly::data::TextureCoordinate> actual,
        const std::array<monopoly::data::TextureCoordinate, Size>& expected)
    {
        return
            actual.size() == expected.size() &&
            std::equal(actual.begin(), actual.end(), expected.begin());
    }


    [[nodiscard]] bool hasUniqueCoordinates(
        std::span<const monopoly::data::TextureCoordinate> coordinates)
    {
        for (std::size_t left = 0; left < coordinates.size(); ++left)
        {
            for (
                std::size_t right = left + 1;
                right < coordinates.size();
                ++right)
            {
                if (coordinates[left] == coordinates[right])
                {
                    return false;
                }
            }
        }

        return true;
    }


    void testMeshDataIdsAndDimensions()
    {
        using namespace monopoly::data;

        expect(
            boardMeshDataId(BoardMeshKind::CityHigh) == 0x00080000U,
            "HMD_board_cityhigh maps to DAT_3D tag 0"
        );
        expect(
            boardMeshDataId(BoardMeshKind::CityMedium) == 0x00080001U,
            "HMD_board_citymed maps to DAT_3D tag 1"
        );
        expect(
            boardMeshDataId(BoardMeshKind::ClassicHigh) == 0x00080002U,
            "HMD_boardhigh maps to DAT_3D tag 2"
        );
        expect(
            boardMeshDataId(BoardMeshKind::ClassicMedium) == 0x00080003U,
            "HMD_boardmed maps to DAT_3D tag 3"
        );
        expect(
            textureDimensions(TextureResolution::Pixels128) ==
                TextureDimensions{128, 128},
            "128 texture resolution has explicit 128x128 dimensions"
        );
        expect(
            textureDimensions(TextureResolution::Pixels256) ==
                TextureDimensions{256, 256},
            "256 texture resolution has explicit 256x256 dimensions"
        );
    }


    void testCoordinateTables()
    {
        using namespace monopoly::data;

        constexpr std::array<TextureCoordinate, 8> usaClassicMedium
        {{
            {448, 0}, {320, 0}, {256, 0}, {192, 0},
            {64, 0}, {0, 0}, {128, 0}, {384, 0}
        }};
        constexpr std::array<TextureCoordinate, 16> usaClassicHigh
        {{
            {192, 0}, {128, 0}, {64, 0}, {448, 0},
            {384, 0}, {320, 0}, {256, 0}, {384, 128},
            {320, 128}, {256, 128}, {192, 128}, {128, 128},
            {64, 128}, {0, 128}, {0, 0}, {448, 128}
        }};
        constexpr std::array<TextureCoordinate, 14> usaCityMedium
        {{
            {320, 0}, {256, 0}, {192, 0}, {128, 0},
            {64, 0}, {0, 0}, {320, 128}, {384, 128},
            {256, 128}, {192, 128}, {64, 128}, {0, 128},
            {128, 128}, {384, 0}
        }};
        constexpr std::array<TextureCoordinate, 22> usaCityHigh
        {{
            {512, 0}, {448, 0}, {128, 128}, {64, 128},
            {0, 128}, {0, 0}, {192, 0}, {128, 0},
            {64, 0}, {640, 0}, {384, 0}, {320, 0},
            {256, 0}, {448, 128}, {384, 128}, {320, 128},
            {256, 128}, {192, 128}, {576, 0}, {576, 128},
            {512, 128}, {640, 128}
        }};

        constexpr std::array<TextureCoordinate, 8> euroClassicMedium
        {{
            {448, 0}, {320, 0}, {256, 0}, {192, 0},
            {64, 0}, {128, 0}, {0, 0}, {384, 0}
        }};
        constexpr std::array<TextureCoordinate, 16> euroClassicHigh
        {{
            {192, 0}, {128, 0}, {64, 0}, {384, 0},
            {320, 0}, {256, 0}, {448, 128}, {0, 128},
            {64, 128}, {448, 0}, {0, 0}, {384, 128},
            {320, 128}, {256, 128}, {192, 128}, {128, 128}
        }};
        constexpr std::array<TextureCoordinate, 14> euroCityMedium
        {{
            {320, 0}, {256, 0}, {192, 0}, {128, 0},
            {64, 0}, {0, 0}, {320, 128}, {384, 128},
            {256, 128}, {192, 128}, {64, 128}, {128, 128},
            {0, 128}, {384, 0}
        }};
        constexpr std::array<TextureCoordinate, 22> euroCityHigh
        {{
            {512, 0}, {448, 0}, {128, 128}, {64, 128},
            {0, 128}, {0, 0}, {192, 0}, {128, 0},
            {64, 0}, {384, 0}, {320, 0}, {256, 0},
            {640, 128}, {576, 128}, {576, 0}, {640, 0},
            {512, 128}, {448, 128}, {384, 128}, {320, 128},
            {256, 128}, {192, 128}
        }};

        expect(
            matches(
                usaTextureCoordinates(BoardMeshKind::ClassicMedium),
                usaClassicMedium),
            "USA classic-medium atlas coordinates are exact"
        );
        expect(
            matches(
                usaTextureCoordinates(BoardMeshKind::ClassicHigh),
                usaClassicHigh),
            "USA classic-high atlas coordinates are exact"
        );
        expect(
            matches(
                usaTextureCoordinates(BoardMeshKind::CityMedium),
                usaCityMedium),
            "USA city-medium atlas coordinates are exact"
        );
        expect(
            matches(
                usaTextureCoordinates(BoardMeshKind::CityHigh),
                usaCityHigh),
            "USA city-high atlas coordinates are exact"
        );
        expect(
            matches(
                europeanTextureCoordinates(BoardMeshKind::ClassicMedium),
                euroClassicMedium),
            "Europe classic-medium atlas coordinates are exact"
        );
        expect(
            matches(
                europeanTextureCoordinates(BoardMeshKind::ClassicHigh),
                euroClassicHigh),
            "Europe classic-high atlas coordinates are exact"
        );
        expect(
            matches(
                europeanTextureCoordinates(BoardMeshKind::CityMedium),
                euroCityMedium),
            "Europe city-medium atlas coordinates are exact"
        );
        expect(
            matches(
                europeanTextureCoordinates(BoardMeshKind::CityHigh),
                euroCityHigh),
            "Europe city-high atlas coordinates are exact"
        );

        constexpr std::array allMeshes
        {
            BoardMeshKind::CityHigh,
            BoardMeshKind::CityMedium,
            BoardMeshKind::ClassicHigh,
            BoardMeshKind::ClassicMedium
        };

        bool everyTableIsUnique = true;

        for (const auto mesh : allMeshes)
        {
            everyTableIsUnique =
                everyTableIsUnique &&
                hasUniqueCoordinates(usaTextureCoordinates(mesh)) &&
                hasUniqueCoordinates(europeanTextureCoordinates(mesh));
        }

        expect(
            everyTableIsUnique,
            "all eight atlas tables contain no duplicate coordinate"
        );
    }


    void testUsaRecipes()
    {
        using namespace monopoly::data;

        const auto embeddedResult = buildUsaTextureRecipe(
            BoardMeshKind::ClassicMedium,
            TextureResolution::Pixels128
        );
        const auto classicMediumResult = buildUsaTextureRecipe(
            BoardMeshKind::ClassicMedium,
            TextureResolution::Pixels256
        );
        const auto classicHighResult = buildUsaTextureRecipe(
            BoardMeshKind::ClassicHigh,
            TextureResolution::Pixels256
        );
        const auto cityMediumResult = buildUsaTextureRecipe(
            BoardMeshKind::CityMedium,
            TextureResolution::Pixels128
        );
        const auto cityHigh128Result = buildUsaTextureRecipe(
            BoardMeshKind::CityHigh,
            TextureResolution::Pixels128
        );
        const auto cityHigh256Result = buildUsaTextureRecipe(
            BoardMeshKind::CityHigh,
            TextureResolution::Pixels256
        );

        expect(
            embeddedResult && classicMediumResult && classicHighResult &&
                cityMediumResult && cityHigh128Result && cityHigh256Result,
            "all source-defined USA recipe enum pairs are accepted"
        );

        if (!embeddedResult || !classicMediumResult || !classicHighResult ||
            !cityMediumResult || !cityHigh128Result || !cityHigh256Result)
        {
            return;
        }

        const auto& embedded = *embeddedResult;
        const auto& classicMedium = *classicMediumResult;
        const auto& classicHigh = *classicHighResult;
        const auto& cityMedium = *cityMediumResult;
        const auto& cityHigh128 = *cityHigh128Result;
        const auto& cityHigh256 = *cityHigh256Result;

        expect(
            embedded.provision == TextureProvision::EmbeddedInMesh &&
                embedded.textures.empty(),
            "USA classic 128 textures remain embedded in the HMD"
        );

        expect(
            classicMedium.textures.size() == 8 &&
                classicMedium.textures[0].role == TextureRole::CityName &&
                classicMedium.textures[3].fileName == "BRD04_256.bmp" &&
                classicMedium.textures[4].role == TextureRole::Common &&
                classicMedium.textures[7].fileName == "CT14_256.bmp",
            "USA classic-medium recipe is 4 names then 4 common textures"
        );

        expect(
            classicHigh.textures.size() == 16 &&
                classicHigh.textures[2].fileName == "BRD03_256.bmp" &&
                classicHigh.textures[3].role == TextureRole::Common &&
                classicHigh.textures[3].fileName == "CT10_256.bmp" &&
                classicHigh.textures[15].fileName == "CT22_256.bmp",
            "USA classic-high recipe is 3 names then 13 common textures"
        );

        expect(
            cityMedium.textures.size() == 14 &&
                cityMedium.textures[0].role == TextureRole::CityPhoto &&
                cityMedium.textures[5].fileName == "CT06_128.BMP" &&
                cityMedium.textures[6].role == TextureRole::CityName &&
                cityMedium.textures[9].fileName == "CT10_128.BMP" &&
                cityMedium.textures[10].role == TextureRole::Common &&
                cityMedium.textures[13].fileName == "CT14_128.bmp",
            "USA city-medium recipe is 6 photos, 4 names, 4 common"
        );

        expect(
            cityHigh128.textures.size() == 22 &&
                cityHigh128.textures[6].fileName == "CT07_256.BMP" &&
                cityHigh128.textures[7].fileName == "CT08_256.BMP" &&
                cityHigh128.textures[8].fileName == "CT09_256.BMP" &&
                cityHigh128.textures[9].role == TextureRole::Common &&
                cityHigh128.textures[21].fileName == "CT22_128.bmp",
            "USA CityHigh128 preserves the CT07/08/09_256 anomaly"
        );

        expect(
            cityHigh256.textures.size() == 22 &&
                cityHigh256.textures[0].fileName == "CT01_256.BMP" &&
                cityHigh256.textures[8].fileName == "CT09_256.BMP" &&
                cityHigh256.textures[9].fileName == "CT10_256.bmp" &&
                cityHigh256.textures[21].fileName == "CT22_256.bmp",
            "USA city-high recipe is 6 photos, 3 names, 13 common"
        );

        bool recipeCoordinatesMatch = true;

        for (const auto* recipe :
            {&classicMedium, &classicHigh, &cityMedium, &cityHigh128})
        {
            const auto positions = usaTextureCoordinates(recipe->mesh);

            for (std::size_t index = 0; index < recipe->textures.size(); ++index)
            {
                recipeCoordinatesMatch =
                    recipeCoordinatesMatch &&
                    recipe->textures[index].coordinate == positions[index];
            }
        }

        expect(
            recipeCoordinatesMatch,
            "USA recipe order remains aligned with every atlas coordinate"
        );
    }


    void testForgedRecipeEnums()
    {
        using namespace monopoly::data;

        constexpr auto forgedMesh = static_cast<BoardMeshKind>(0xFFFFU);
        constexpr auto forgedResolution =
            static_cast<TextureResolution>(999U);

        expect(
            textureDimensions(forgedResolution) == TextureDimensions{} &&
                cityPhotoTextureNames(forgedResolution).empty() &&
                usaCityNameTextureNames(
                    BoardMeshKind::CityHigh,
                    forgedResolution).empty() &&
                commonTextureNames(
                    BoardMeshKind::ClassicMedium,
                    forgedResolution).empty() &&
                europeanLanguageTextureNames(
                    BoardMeshKind::ClassicHigh,
                    forgedResolution).empty() &&
                europeanBoardOverlayTextureNames(
                    BoardMeshKind::ClassicMedium,
                    forgedResolution).empty() &&
                europeanCurrencyTextureNames(
                    BoardMeshKind::CityHigh,
                    forgedResolution).empty() &&
                europeanGoOverlayTextureName(forgedResolution).empty(),
            "public texture helpers reject a forged resolution consistently"
        );

        expect(
            usaTextureCoordinates(forgedMesh).empty() &&
                europeanTextureCoordinates(forgedMesh).empty() &&
                usaCityNameTextureNames(
                    forgedMesh,
                    TextureResolution::Pixels128).empty() &&
                commonTextureNames(
                    forgedMesh,
                    TextureResolution::Pixels128).empty() &&
                europeanLanguageTextureNames(
                    forgedMesh,
                    TextureResolution::Pixels128).empty() &&
                europeanBoardOverlayTextureNames(
                    forgedMesh,
                    TextureResolution::Pixels128).empty() &&
                europeanCurrencyTextureNames(
                    forgedMesh,
                    TextureResolution::Pixels128).empty(),
            "public texture helpers reject a forged mesh consistently"
        );

        const auto usaMesh = buildUsaTextureRecipe(
            forgedMesh,
            TextureResolution::Pixels128
        );
        const auto usaResolution = buildUsaTextureRecipe(
            BoardMeshKind::ClassicMedium,
            forgedResolution
        );
        const auto europeMesh = buildEuropeanTextureRecipe(
            forgedMesh,
            TextureResolution::Pixels256
        );
        const auto europeResolution = buildEuropeanTextureRecipe(
            BoardMeshKind::CityHigh,
            forgedResolution
        );

        expect(
            !usaMesh &&
                usaMesh.error().code ==
                    TextureCatalogErrorCode::InvalidBoardMeshKind &&
                usaMesh.error().rawValue == 0xFFFFU &&
                textureCatalogErrorCodeName(usaMesh.error().code) ==
                    "InvalidBoardMeshKind",
            "USA recipe rejects a forged board mesh before catalog indexing"
        );
        expect(
            !usaResolution &&
                usaResolution.error().code ==
                    TextureCatalogErrorCode::InvalidTextureResolution &&
                usaResolution.error().rawValue == 999U,
            "USA recipe rejects a forged resolution before catalog indexing"
        );
        expect(
            !europeMesh &&
                europeMesh.error().code ==
                    TextureCatalogErrorCode::InvalidBoardMeshKind &&
                europeMesh.error().rawValue == 0xFFFFU,
            "Europe recipe rejects a forged board mesh before catalog indexing"
        );
        expect(
            !europeResolution &&
                europeResolution.error().code ==
                    TextureCatalogErrorCode::InvalidTextureResolution &&
                europeResolution.error().rawValue == 999U &&
                textureCatalogErrorCodeName(europeResolution.error().code) ==
                    "InvalidTextureResolution",
            "Europe recipe rejects a forged resolution before catalog indexing"
        );
    }


    void testEuropeanRecipes()
    {
        using namespace monopoly::data;

        const auto classicMediumResult = buildEuropeanTextureRecipe(
            BoardMeshKind::ClassicMedium,
            TextureResolution::Pixels128
        );
        const auto classicHighResult = buildEuropeanTextureRecipe(
            BoardMeshKind::ClassicHigh,
            TextureResolution::Pixels256
        );
        const auto cityMediumResult = buildEuropeanTextureRecipe(
            BoardMeshKind::CityMedium,
            TextureResolution::Pixels128
        );
        const auto cityHighResult = buildEuropeanTextureRecipe(
            BoardMeshKind::CityHigh,
            TextureResolution::Pixels256
        );

        expect(
            classicMediumResult && classicHighResult &&
                cityMediumResult && cityHighResult,
            "all source-defined Europe recipe enum pairs are accepted"
        );

        if (!classicMediumResult || !classicHighResult ||
            !cityMediumResult || !cityHighResult)
        {
            return;
        }

        const auto& classicMedium = *classicMediumResult;
        const auto& classicHigh = *classicHighResult;
        const auto& cityMedium = *cityMediumResult;
        const auto& cityHigh = *cityHighResult;

        expect(
            classicMedium.textures.size() == 8 &&
                classicMedium.textures[0].fileName == "LANG01_128.BMP" &&
                classicMedium.textures[0].overlay.has_value() &&
                classicMedium.textures[0].overlay->role ==
                    TextureRole::BoardOverlay &&
                classicMedium.textures[0].overlay->fileName ==
                    "BRD01_128.BMP" &&
                classicMedium.textures[6].overlay.has_value() &&
                classicMedium.textures[6].overlay->role ==
                    TextureRole::GoCurrencyOverlay &&
                classicMedium.textures[7].role == TextureRole::Currency,
            "Europe classic-medium composes 7 language and 1 currency textures"
        );

        expect(
            classicHigh.textures.size() == 16 &&
                classicHigh.textures[2].overlay.has_value() &&
                classicHigh.textures[2].overlay->fileName ==
                    "BRD03_256.BMP" &&
                !classicHigh.textures[3].overlay.has_value() &&
                classicHigh.textures[10].overlay.has_value() &&
                classicHigh.textures[10].overlay->fileName ==
                    "GO_OVERLAY256.bmp" &&
                classicHigh.textures[11].fileName == "CUR01_256.BMP" &&
                classicHigh.textures[15].fileName == "CUR05_256.BMP",
            "Europe classic-high composes 11 language and 5 currency textures"
        );

        expect(
            cityMedium.textures.size() == 14 &&
                cityMedium.textures[0].role == TextureRole::CityPhoto &&
                cityMedium.textures[6].role == TextureRole::CityName &&
                cityMedium.textures[10].fileName == "LANG05_128.BMP" &&
                !cityMedium.textures[10].overlay.has_value() &&
                cityMedium.textures[12].fileName == "LANG07_128.BMP" &&
                cityMedium.textures[12].overlay.has_value() &&
                cityMedium.textures[12].overlay->role ==
                    TextureRole::GoCurrencyOverlay &&
                cityMedium.textures[13].fileName == "CUR01_128.BMP",
            "Europe city-medium uses language indices 4..6 then currency"
        );

        expect(
            cityHigh.textures.size() == 22 &&
                cityHigh.textures[9].fileName == "LANG04_256.BMP" &&
                !cityHigh.textures[9].overlay.has_value() &&
                cityHigh.textures[16].fileName == "LANG11_256.BMP" &&
                cityHigh.textures[16].overlay.has_value() &&
                cityHigh.textures[16].overlay->role ==
                    TextureRole::GoCurrencyOverlay &&
                cityHigh.textures[17].fileName == "CUR01_256.BMP" &&
                cityHigh.textures[21].fileName == "CUR05_256.BMP",
            "Europe city-high uses language indices 3..10 then 5 currencies"
        );

        bool recipeCoordinatesMatch = true;

        for (const auto* recipe :
            {&classicMedium, &classicHigh, &cityMedium, &cityHigh})
        {
            const auto positions = europeanTextureCoordinates(recipe->mesh);

            for (std::size_t index = 0; index < recipe->textures.size(); ++index)
            {
                recipeCoordinatesMatch =
                    recipeCoordinatesMatch &&
                    recipe->textures[index].coordinate == positions[index];
            }
        }

        expect(
            recipeCoordinatesMatch,
            "Europe recipe order remains aligned with every atlas coordinate"
        );
    }


    void testTwoDimensionalBoardNames()
    {
        using namespace monopoly::data;

        constexpr std::array<std::string_view, 39> expected
        {{
            "2DVIEW01.BMP", "2DVIEW02.BMP", "2DVIEW03.BMP",
            "2DVIEW04.BMP", "2DVIEW05.BMP", "2DVIEW06.BMP",
            "2DVIEW07.BMP", "2DVIEW08.BMP", "2DVIEW09.BMP",
            "2DVIEW10.BMP", "2DVIEW11.BMP", "2DVIEW12.BMP",
            "2DVIEW13.BMP", "2DVIEW14.BMP", "2DVIEW15.BMP",
            "2DVIEW16.BMP", "2DVIEW17.BMP", "2DVIEW18.BMP",
            "2DVIEW19.BMP", "2DVIEW20.BMP", "2DVIEW21.BMP",
            "2DVIEW22.BMP", "2DVIEW23.BMP", "2DVIEW24.BMP",
            "2DVIEW25.BMP", "2DVIEW26.BMP", "2DVIEW27.BMP",
            "2DVIEW28.BMP", "2DVIEW29.BMP", "2DVIEW30.BMP",
            "2DVIEW31.BMP", "2DVIEW32.BMP", "2DVIEW33.BMP",
            "2DVIEW34.BMP", "2DVIEW35.BMP", "2DVIEW36.BMP",
            "2DVIEW37.BMP", "2DVIEW38.BMP", "2DVIEW39.BMP"
        }};

        const auto actual = twoDimensionalBoardTextureNames();

        expect(
            actual.size() == expected.size() &&
                std::equal(actual.begin(), actual.end(), expected.begin()),
            "the complete ordered 39-item custom 2D board manifest is exact"
        );
    }


    void testPhysicalTextureCorpusManifest()
    {
        using namespace monopoly::data;

        const auto& manifest = legacyTextureCorpusManifest();
        std::unordered_set<std::string_view> uniquePaths;
        std::size_t pixels128 = 0;
        std::size_t pixels256 = 0;
        std::size_t indexed8 = 0;
        std::size_t trueColor24 = 0;
        std::size_t photos = 0;
        std::size_t cityNames = 0;
        std::size_t common = 0;
        std::size_t language = 0;
        std::size_t boardOverlays = 0;
        std::size_t currencies = 0;
        std::size_t goOverlays = 0;
        bool dimensionsAndCompressionMatch = true;

        for (const auto& asset : manifest)
        {
            uniquePaths.insert(asset.relativePath);

            if (asset.resolution == TextureResolution::Pixels128)
            {
                ++pixels128;
            }
            else
            {
                ++pixels256;
            }

            if (asset.expectedBitsPerPixel == 8)
            {
                ++indexed8;
            }
            else if (asset.expectedBitsPerPixel == 24)
            {
                ++trueColor24;
            }

            dimensionsAndCompressionMatch =
                dimensionsAndCompressionMatch &&
                asset.expectedDimensions ==
                    textureDimensions(asset.resolution) &&
                asset.expectedCompression == 0;

            switch (asset.role)
            {
                case TextureRole::CityPhoto:
                    ++photos;
                    break;

                case TextureRole::CityName:
                    ++cityNames;
                    break;

                case TextureRole::Common:
                    ++common;
                    break;

                case TextureRole::Language:
                    ++language;
                    break;

                case TextureRole::BoardOverlay:
                    ++boardOverlays;
                    break;

                case TextureRole::Currency:
                    ++currencies;
                    break;

                case TextureRole::GoCurrencyOverlay:
                    ++goOverlays;
                    break;

                case TextureRole::TwoDimensionalBoard:
                    break;
            }
        }

        expect(
            manifest.size() == LegacyTextureCorpusSize &&
                uniquePaths.size() == LegacyTextureCorpusSize,
            "physical TexInfo corpus contains 1001 unique relative BMP paths"
        );
        expect(
            pixels128 == 497 && pixels256 == 504,
            "physical corpus resolution distribution is 497x128 and 504x256"
        );
        expect(
            indexed8 == 881 && trueColor24 == 120,
            "physical corpus depth distribution is 881 indexed and 120 true-color"
        );
        expect(
            dimensionsAndCompressionMatch,
            "every physical BMP contract is square and BI_RGB"
        );
        expect(
            photos == 120 &&
                cityNames == 147 &&
                common == 34 &&
                language == 324 &&
                boardOverlays == 168 &&
                currencies == 156 &&
                goOverlays == 52,
            "physical corpus role distribution matches all source folders"
        );

        const auto contains = [&uniquePaths](std::string_view path)
        {
            return uniquePaths.contains(path);
        };

        expect(
            contains("Boards/Board11/High/BRD03_256.bmp") &&
                contains("Cities/City10/Photos/CT06_128.bmp") &&
                contains("Cities/Common/High/CT22_256.bmp") &&
                contains("Currency/Curr12/Medium/GO_OVERLAY128.bmp") &&
                contains("Languages/Lang08/High/LANG11_256.bmp"),
            "physical manifest covers the last indexed folder in each family"
        );
        expect(
            contains("Cities/City01/High/CT07_128.bmp"),
            "physical manifest keeps the unused CityHigh 128 counterpart"
        );
    }


    [[nodiscard]] std::uint16_t readLittleEndian16(
        const std::array<unsigned char, 54>& header,
        std::size_t offset)
    {
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(header[offset]) |
            (static_cast<std::uint16_t>(header[offset + 1]) << 8U)
        );
    }


    [[nodiscard]] std::uint32_t readLittleEndian32(
        const std::array<unsigned char, 54>& header,
        std::size_t offset)
    {
        return
            static_cast<std::uint32_t>(header[offset]) |
            (static_cast<std::uint32_t>(header[offset + 1]) << 8U) |
            (static_cast<std::uint32_t>(header[offset + 2]) << 16U) |
            (static_cast<std::uint32_t>(header[offset + 3]) << 24U);
    }


    void testPhysicalTextureFiles(
        const std::filesystem::path& legacyMonopolyRoot)
    {
        using namespace monopoly::data;

        std::size_t invalidFiles = 0;
        std::size_t reportedDetails = 0;

        const auto reportInvalid =
            [&](const LegacyTextureAsset& asset, std::string_view reason)
        {
            ++invalidFiles;

            if (reportedDetails < 10)
            {
                ++reportedDetails;
                std::cerr
                    << "[DETAIL] "
                    << asset.relativePath
                    << ": "
                    << reason
                    << '\n';
            }
        };

        for (const auto& asset : legacyTextureCorpusManifest())
        {
            const auto path = legacyMonopolyRoot / asset.relativePath;
            std::ifstream input{path, std::ios::binary};

            if (!input)
            {
                reportInvalid(asset, "missing file");
                continue;
            }

            std::array<unsigned char, 54> header{};
            input.read(
                reinterpret_cast<char*>(header.data()),
                static_cast<std::streamsize>(header.size())
            );

            if (input.gcount() !=
                static_cast<std::streamsize>(header.size()))
            {
                reportInvalid(asset, "truncated BMP header");
                continue;
            }

            const auto width = static_cast<std::int32_t>(
                readLittleEndian32(header, 18)
            );
            const auto height = static_cast<std::int32_t>(
                readLittleEndian32(header, 22)
            );
            const auto planes = readLittleEndian16(header, 26);
            const auto bitsPerPixel = readLittleEndian16(header, 28);
            const auto compression = readLittleEndian32(header, 30);

            const bool valid =
                header[0] == static_cast<unsigned char>('B') &&
                header[1] == static_cast<unsigned char>('M') &&
                readLittleEndian32(header, 14) >= 40 &&
                width == asset.expectedDimensions.width &&
                height == asset.expectedDimensions.height &&
                planes == 1 &&
                bitsPerPixel == asset.expectedBitsPerPixel &&
                compression == asset.expectedCompression;

            if (!valid)
            {
                reportInvalid(asset, "unexpected BMP metadata");
            }
        }

        expect(
            invalidFiles == 0,
            "optional source audit validates all 1001 BMP files and headers"
        );
    }
}


static_assert(
    monopoly::data::boardMeshTag(
        monopoly::data::BoardMeshKind::CityHigh) == 0
);
static_assert(
    monopoly::data::boardMeshTag(
        monopoly::data::BoardMeshKind::ClassicMedium) == 3
);
static_assert(
    monopoly::data::boardMeshDataId(
        monopoly::data::BoardMeshKind::ClassicHigh) == 0x00080002U
);
static_assert(
    monopoly::data::textureDimensions(
        monopoly::data::TextureResolution::Pixels256) ==
        monopoly::data::TextureDimensions{256, 256}
);


int main(int argumentCount, char* arguments[])
{
    std::cout
        << "Monopoly TexInfo catalog tests\n"
        << "==============================\n";

    testMeshDataIdsAndDimensions();
    testCoordinateTables();
    testUsaRecipes();
    testForgedRecipeEnums();
    testEuropeanRecipes();
    testTwoDimensionalBoardNames();
    testPhysicalTextureCorpusManifest();

    if (argumentCount >= 2)
    {
        testPhysicalTextureFiles(arguments[1]);
    }
    else
    {
        std::cout
            << "[SKIP] optional physical BMP audit (pass the legacy "
               "monopoly directory as argument)\n";
    }

    std::cout << '\n';

    if (failures != 0)
    {
        std::cerr << failures << " texture catalog test(s) failed.\n";
        return 1;
    }

    std::cout << "All texture catalog tests passed.\n";
    return 0;
}
