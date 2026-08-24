#include "TextureCatalog.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <utility>

namespace monopoly::data
{
    namespace
    {
        constexpr std::array<TextureCoordinate, 8> UsaClassicMediumPositions
        {{
            {448, 0},
            {320, 0},
            {256, 0},
            {192, 0},
            {64, 0},
            {0, 0},
            {128, 0},
            {384, 0}
        }};

        constexpr std::array<TextureCoordinate, 16> UsaClassicHighPositions
        {{
            {192, 0},
            {128, 0},
            {64, 0},
            {448, 0},
            {384, 0},
            {320, 0},
            {256, 0},
            {384, 128},
            {320, 128},
            {256, 128},
            {192, 128},
            {128, 128},
            {64, 128},
            {0, 128},
            {0, 0},
            {448, 128}
        }};

        constexpr std::array<TextureCoordinate, 14> UsaCityMediumPositions
        {{
            {320, 0},
            {256, 0},
            {192, 0},
            {128, 0},
            {64, 0},
            {0, 0},
            {320, 128},
            {384, 128},
            {256, 128},
            {192, 128},
            {64, 128},
            {0, 128},
            {128, 128},
            {384, 0}
        }};

        constexpr std::array<TextureCoordinate, 22> UsaCityHighPositions
        {{
            {512, 0},
            {448, 0},
            {128, 128},
            {64, 128},
            {0, 128},
            {0, 0},
            {192, 0},
            {128, 0},
            {64, 0},
            {640, 0},
            {384, 0},
            {320, 0},
            {256, 0},
            {448, 128},
            {384, 128},
            {320, 128},
            {256, 128},
            {192, 128},
            {576, 0},
            {576, 128},
            {512, 128},
            {640, 128}
        }};

        constexpr std::array<TextureCoordinate, 8> EuroClassicMediumPositions
        {{
            {448, 0},
            {320, 0},
            {256, 0},
            {192, 0},
            {64, 0},
            {128, 0},
            {0, 0},
            {384, 0}
        }};

        constexpr std::array<TextureCoordinate, 16> EuroClassicHighPositions
        {{
            {192, 0},
            {128, 0},
            {64, 0},
            {384, 0},
            {320, 0},
            {256, 0},
            {448, 128},
            {0, 128},
            {64, 128},
            {448, 0},
            {0, 0},
            {384, 128},
            {320, 128},
            {256, 128},
            {192, 128},
            {128, 128}
        }};

        constexpr std::array<TextureCoordinate, 14> EuroCityMediumPositions
        {{
            {320, 0},
            {256, 0},
            {192, 0},
            {128, 0},
            {64, 0},
            {0, 0},
            {320, 128},
            {384, 128},
            {256, 128},
            {192, 128},
            {64, 128},
            {128, 128},
            {0, 128},
            {384, 0}
        }};

        constexpr std::array<TextureCoordinate, 22> EuroCityHighPositions
        {{
            {512, 0},
            {448, 0},
            {128, 128},
            {64, 128},
            {0, 128},
            {0, 0},
            {192, 0},
            {128, 0},
            {64, 0},
            {384, 0},
            {320, 0},
            {256, 0},
            {640, 128},
            {576, 128},
            {576, 0},
            {640, 0},
            {512, 128},
            {448, 128},
            {384, 128},
            {320, 128},
            {256, 128},
            {192, 128}
        }};

        constexpr std::array<std::string_view, 4> CommonMedium128
        {{
            "CT11_128.bmp",
            "CT12_128.bmp",
            "CT13_128.bmp",
            "CT14_128.bmp"
        }};

        constexpr std::array<std::string_view, 4> CommonMedium256
        {{
            "CT11_256.bmp",
            "CT12_256.bmp",
            "CT13_256.bmp",
            "CT14_256.bmp"
        }};

        constexpr std::array<std::string_view, 13> CommonHigh128
        {{
            "CT10_128.bmp",
            "CT11_128.bmp",
            "CT12_128.bmp",
            "CT13_128.bmp",
            "CT14_128.bmp",
            "CT15_128.bmp",
            "CT16_128.bmp",
            "CT17_128.bmp",
            "CT18_128.bmp",
            "CT19_128.bmp",
            "CT20_128.bmp",
            "CT21_128.bmp",
            "CT22_128.bmp"
        }};

        constexpr std::array<std::string_view, 13> CommonHigh256
        {{
            "CT10_256.bmp",
            "CT11_256.bmp",
            "CT12_256.bmp",
            "CT13_256.bmp",
            "CT14_256.bmp",
            "CT15_256.bmp",
            "CT16_256.bmp",
            "CT17_256.bmp",
            "CT18_256.bmp",
            "CT19_256.bmp",
            "CT20_256.bmp",
            "CT21_256.bmp",
            "CT22_256.bmp"
        }};

        constexpr std::array<std::string_view, 6> CityPhotos128
        {{
            "CT01_128.BMP",
            "CT02_128.BMP",
            "CT03_128.BMP",
            "CT04_128.BMP",
            "CT05_128.BMP",
            "CT06_128.BMP"
        }};

        constexpr std::array<std::string_view, 6> CityPhotos256
        {{
            "CT01_256.BMP",
            "CT02_256.BMP",
            "CT03_256.BMP",
            "CT04_256.BMP",
            "CT05_256.BMP",
            "CT06_256.BMP"
        }};

        constexpr std::array<std::string_view, 4> ClassicMediumNames256
        {{
            "BRD01_256.bmp",
            "BRD02_256.bmp",
            "BRD03_256.bmp",
            "BRD04_256.bmp"
        }};

        constexpr std::array<std::string_view, 3> ClassicHighNames256
        {{
            "BRD01_256.bmp",
            "BRD02_256.bmp",
            "BRD03_256.bmp"
        }};

        constexpr std::array<std::string_view, 4> CityMediumNames128
        {{
            "CT07_128.BMP",
            "CT08_128.BMP",
            "CT09_128.BMP",
            "CT10_128.BMP"
        }};

        constexpr std::array<std::string_view, 4> CityMediumNames256
        {{
            "CT07_256.BMP",
            "CT08_256.BMP",
            "CT09_256.BMP",
            "CT10_256.BMP"
        }};

        // Anomalie historique volontaire : TexInfo.cpp nomme ces trois
        // textures *_256 même dans la table CityHigh128.
        constexpr std::array<std::string_view, 3> CityHighNames128
        {{
            "CT07_256.BMP",
            "CT08_256.BMP",
            "CT09_256.BMP"
        }};

        constexpr std::array<std::string_view, 3> CityHighNames256
        {{
            "CT07_256.BMP",
            "CT08_256.BMP",
            "CT09_256.BMP"
        }};

        // Ces fichiers existent dans le corpus physique, même si le caller
        // historique ne les sélectionne pas à cause de l'anomalie ci-dessus.
        constexpr std::array<std::string_view, 3> PhysicalCityHighNames128
        {{
            "CT07_128.bmp",
            "CT08_128.bmp",
            "CT09_128.bmp"
        }};

        constexpr std::array<std::string_view, 4> EuroMediumBoards128
        {{
            "BRD01_128.BMP",
            "BRD02_128.BMP",
            "BRD03_128.BMP",
            "BRD04_128.BMP"
        }};

        constexpr std::array<std::string_view, 4> EuroMediumBoards256
        {{
            "BRD01_256.BMP",
            "BRD02_256.BMP",
            "BRD03_256.BMP",
            "BRD04_256.BMP"
        }};

        constexpr std::array<std::string_view, 3> EuroHighBoards128
        {{
            "BRD01_128.BMP",
            "BRD02_128.BMP",
            "BRD03_128.BMP"
        }};

        constexpr std::array<std::string_view, 3> EuroHighBoards256
        {{
            "BRD01_256.BMP",
            "BRD02_256.BMP",
            "BRD03_256.BMP"
        }};

        constexpr std::array<std::string_view, 1> EuroMediumCurrencies128
        {{
            "CUR01_128.BMP"
        }};

        constexpr std::array<std::string_view, 1> EuroMediumCurrencies256
        {{
            "CUR01_256.BMP"
        }};

        constexpr std::array<std::string_view, 5> EuroHighCurrencies128
        {{
            "CUR01_128.BMP",
            "CUR02_128.BMP",
            "CUR03_128.BMP",
            "CUR04_128.BMP",
            "CUR05_128.BMP"
        }};

        constexpr std::array<std::string_view, 5> EuroHighCurrencies256
        {{
            "CUR01_256.BMP",
            "CUR02_256.BMP",
            "CUR03_256.BMP",
            "CUR04_256.BMP",
            "CUR05_256.BMP"
        }};

        constexpr std::array<std::string_view, 7> EuroMediumLanguage128
        {{
            "LANG01_128.BMP",
            "LANG02_128.BMP",
            "LANG03_128.BMP",
            "LANG04_128.BMP",
            "LANG05_128.BMP",
            "LANG06_128.BMP",
            "LANG07_128.BMP"
        }};

        constexpr std::array<std::string_view, 7> EuroMediumLanguage256
        {{
            "LANG01_256.BMP",
            "LANG02_256.BMP",
            "LANG03_256.BMP",
            "LANG04_256.BMP",
            "LANG05_256.BMP",
            "LANG06_256.BMP",
            "LANG07_256.BMP"
        }};

        constexpr std::array<std::string_view, 11> EuroHighLanguage128
        {{
            "LANG01_128.BMP",
            "LANG02_128.BMP",
            "LANG03_128.BMP",
            "LANG04_128.BMP",
            "LANG05_128.BMP",
            "LANG06_128.BMP",
            "LANG07_128.BMP",
            "LANG08_128.BMP",
            "LANG09_128.BMP",
            "LANG10_128.BMP",
            "LANG11_128.BMP"
        }};

        constexpr std::array<std::string_view, 11> EuroHighLanguage256
        {{
            "LANG01_256.BMP",
            "LANG02_256.BMP",
            "LANG03_256.BMP",
            "LANG04_256.BMP",
            "LANG05_256.BMP",
            "LANG06_256.BMP",
            "LANG07_256.BMP",
            "LANG08_256.BMP",
            "LANG09_256.BMP",
            "LANG10_256.BMP",
            "LANG11_256.BMP"
        }};

        constexpr std::array<std::string_view, 39> TwoDimensionalBoards
        {{
            "2DVIEW01.BMP",
            "2DVIEW02.BMP",
            "2DVIEW03.BMP",
            "2DVIEW04.BMP",
            "2DVIEW05.BMP",
            "2DVIEW06.BMP",
            "2DVIEW07.BMP",
            "2DVIEW08.BMP",
            "2DVIEW09.BMP",
            "2DVIEW10.BMP",
            "2DVIEW11.BMP",
            "2DVIEW12.BMP",
            "2DVIEW13.BMP",
            "2DVIEW14.BMP",
            "2DVIEW15.BMP",
            "2DVIEW16.BMP",
            "2DVIEW17.BMP",
            "2DVIEW18.BMP",
            "2DVIEW19.BMP",
            "2DVIEW20.BMP",
            "2DVIEW21.BMP",
            "2DVIEW22.BMP",
            "2DVIEW23.BMP",
            "2DVIEW24.BMP",
            "2DVIEW25.BMP",
            "2DVIEW26.BMP",
            "2DVIEW27.BMP",
            "2DVIEW28.BMP",
            "2DVIEW29.BMP",
            "2DVIEW30.BMP",
            "2DVIEW31.BMP",
            "2DVIEW32.BMP",
            "2DVIEW33.BMP",
            "2DVIEW34.BMP",
            "2DVIEW35.BMP",
            "2DVIEW36.BMP",
            "2DVIEW37.BMP",
            "2DVIEW38.BMP",
            "2DVIEW39.BMP"
        }};


        void appendTextureReferences(
            BoardTextureRecipe& recipe,
            std::span<const TextureCoordinate> coordinates,
            std::size_t coordinateOffset,
            std::span<const std::string_view> names,
            TextureRole role,
            TextureLocation location)
        {
            for (std::size_t index = 0; index < names.size(); ++index)
            {
                recipe.textures.push_back(
                    TextureReference
                    {
                        .coordinate = coordinates[coordinateOffset + index],
                        .resolution = recipe.resolution,
                        .role = role,
                        .location = location,
                        .fileName = names[index]
                    }
                );
            }
        }


        [[nodiscard]] std::string twoDigitIndex(std::size_t index)
        {
            std::string result;
            result.reserve(2);
            result.push_back(
                static_cast<char>('0' + ((index / 10) % 10))
            );
            result.push_back(static_cast<char>('0' + (index % 10)));
            return result;
        }


        [[nodiscard]] std::string physicalBmpName(std::string_view sourceName)
        {
            std::string result{sourceName};

            if (result.ends_with(".BMP"))
            {
                result.replace(result.size() - 4, 4, ".bmp");
            }

            return result;
        }


        [[nodiscard]] std::string indexedTextureDirectory(
            std::string_view root,
            std::string_view prefix,
            std::size_t index,
            std::string_view detail)
        {
            std::string result;
            result.reserve(
                root.size() + prefix.size() + detail.size() + 6
            );
            result.append(root);
            result.push_back('/');
            result.append(prefix);
            result.append(twoDigitIndex(index));
            result.push_back('/');
            result.append(detail);
            return result;
        }


        void appendCorpusAssets(
            std::vector<LegacyTextureAsset>& manifest,
            std::string_view directory,
            std::span<const std::string_view> names,
            TextureRole role,
            TextureLocation location,
            TextureResolution resolution,
            std::uint16_t bitsPerPixel)
        {
            for (const auto name : names)
            {
                std::string relativePath;
                relativePath.reserve(directory.size() + name.size() + 1);
                relativePath.append(directory);
                relativePath.push_back('/');
                relativePath.append(physicalBmpName(name));

                manifest.push_back(
                    LegacyTextureAsset
                    {
                        .relativePath = std::move(relativePath),
                        .role = role,
                        .location = location,
                        .resolution = resolution,
                        .expectedDimensions = textureDimensions(resolution),
                        .expectedBitsPerPixel = bitsPerPixel,
                        .expectedCompression = 0
                    }
                );
            }
        }


        void appendCorpusAsset(
            std::vector<LegacyTextureAsset>& manifest,
            std::string_view directory,
            std::string_view name,
            TextureRole role,
            TextureLocation location,
            TextureResolution resolution,
            std::uint16_t bitsPerPixel)
        {
            const std::array names{name};
            appendCorpusAssets(
                manifest,
                directory,
                names,
                role,
                location,
                resolution,
                bitsPerPixel
            );
        }
    }


    std::string_view textureCatalogErrorCodeName(
        TextureCatalogErrorCode code) noexcept
    {
        switch (code)
        {
            case TextureCatalogErrorCode::InvalidBoardMeshKind:
                return "InvalidBoardMeshKind";

            case TextureCatalogErrorCode::InvalidTextureResolution:
                return "InvalidTextureResolution";
        }

        return "InvalidTextureCatalogErrorCode";
    }


    std::span<const TextureCoordinate> usaTextureCoordinates(
        BoardMeshKind mesh) noexcept
    {
        switch (mesh)
        {
            case BoardMeshKind::CityHigh:
                return UsaCityHighPositions;

            case BoardMeshKind::CityMedium:
                return UsaCityMediumPositions;

            case BoardMeshKind::ClassicHigh:
                return UsaClassicHighPositions;

            case BoardMeshKind::ClassicMedium:
                return UsaClassicMediumPositions;
        }

        return {};
    }


    std::span<const TextureCoordinate> europeanTextureCoordinates(
        BoardMeshKind mesh) noexcept
    {
        switch (mesh)
        {
            case BoardMeshKind::CityHigh:
                return EuroCityHighPositions;

            case BoardMeshKind::CityMedium:
                return EuroCityMediumPositions;

            case BoardMeshKind::ClassicHigh:
                return EuroClassicHighPositions;

            case BoardMeshKind::ClassicMedium:
                return EuroClassicMediumPositions;
        }

        return {};
    }


    std::span<const std::string_view> cityPhotoTextureNames(
        TextureResolution resolution) noexcept
    {
        if (!isValidTextureResolution(resolution))
        {
            return {};
        }

        return resolution == TextureResolution::Pixels256
            ? std::span<const std::string_view>{CityPhotos256}
            : std::span<const std::string_view>{CityPhotos128};
    }


    std::span<const std::string_view> usaCityNameTextureNames(
        BoardMeshKind mesh,
        TextureResolution resolution) noexcept
    {
        if (!isValidTextureResolution(resolution))
        {
            return {};
        }

        switch (mesh)
        {
            case BoardMeshKind::CityHigh:
                return resolution == TextureResolution::Pixels256
                    ? std::span<const std::string_view>{CityHighNames256}
                    : std::span<const std::string_view>{CityHighNames128};

            case BoardMeshKind::CityMedium:
                return resolution == TextureResolution::Pixels256
                    ? std::span<const std::string_view>{CityMediumNames256}
                    : std::span<const std::string_view>{CityMediumNames128};

            case BoardMeshKind::ClassicHigh:
                return resolution == TextureResolution::Pixels256
                    ? std::span<const std::string_view>{ClassicHighNames256}
                    : std::span<const std::string_view>{};

            case BoardMeshKind::ClassicMedium:
                return resolution == TextureResolution::Pixels256
                    ? std::span<const std::string_view>{ClassicMediumNames256}
                    : std::span<const std::string_view>{};
        }

        return {};
    }


    std::span<const std::string_view> commonTextureNames(
        BoardMeshKind mesh,
        TextureResolution resolution) noexcept
    {
        if (!isValidBoardMeshKind(mesh) ||
            !isValidTextureResolution(resolution))
        {
            return {};
        }

        if (isHighDetailMesh(mesh))
        {
            return resolution == TextureResolution::Pixels256
                ? std::span<const std::string_view>{CommonHigh256}
                : std::span<const std::string_view>{CommonHigh128};
        }

        return resolution == TextureResolution::Pixels256
            ? std::span<const std::string_view>{CommonMedium256}
            : std::span<const std::string_view>{CommonMedium128};
    }


    std::span<const std::string_view> europeanLanguageTextureNames(
        BoardMeshKind mesh,
        TextureResolution resolution) noexcept
    {
        if (!isValidBoardMeshKind(mesh) ||
            !isValidTextureResolution(resolution))
        {
            return {};
        }

        if (isHighDetailMesh(mesh))
        {
            return resolution == TextureResolution::Pixels256
                ? std::span<const std::string_view>{EuroHighLanguage256}
                : std::span<const std::string_view>{EuroHighLanguage128};
        }

        return resolution == TextureResolution::Pixels256
            ? std::span<const std::string_view>{EuroMediumLanguage256}
            : std::span<const std::string_view>{EuroMediumLanguage128};
    }


    std::span<const std::string_view> europeanBoardOverlayTextureNames(
        BoardMeshKind mesh,
        TextureResolution resolution) noexcept
    {
        if (!isValidBoardMeshKind(mesh) ||
            !isValidTextureResolution(resolution))
        {
            return {};
        }

        if (isHighDetailMesh(mesh))
        {
            return resolution == TextureResolution::Pixels256
                ? std::span<const std::string_view>{EuroHighBoards256}
                : std::span<const std::string_view>{EuroHighBoards128};
        }

        return resolution == TextureResolution::Pixels256
            ? std::span<const std::string_view>{EuroMediumBoards256}
            : std::span<const std::string_view>{EuroMediumBoards128};
    }


    std::span<const std::string_view> europeanCurrencyTextureNames(
        BoardMeshKind mesh,
        TextureResolution resolution) noexcept
    {
        if (!isValidBoardMeshKind(mesh) ||
            !isValidTextureResolution(resolution))
        {
            return {};
        }

        if (isHighDetailMesh(mesh))
        {
            return resolution == TextureResolution::Pixels256
                ? std::span<const std::string_view>{EuroHighCurrencies256}
                : std::span<const std::string_view>{EuroHighCurrencies128};
        }

        return resolution == TextureResolution::Pixels256
            ? std::span<const std::string_view>{EuroMediumCurrencies256}
            : std::span<const std::string_view>{EuroMediumCurrencies128};
    }


    std::string_view europeanGoOverlayTextureName(
        TextureResolution resolution) noexcept
    {
        if (!isValidTextureResolution(resolution))
        {
            return {};
        }

        return resolution == TextureResolution::Pixels256
            ? "GO_OVERLAY256.bmp"
            : "GO_OVERLAY128.bmp";
    }


    std::span<const std::string_view>
        twoDimensionalBoardTextureNames() noexcept
    {
        return TwoDimensionalBoards;
    }


    const std::vector<LegacyTextureAsset>& legacyTextureCorpusManifest()
    {
        static const auto manifest = []
        {
            std::vector<LegacyTextureAsset> result;
            result.reserve(LegacyTextureCorpusSize);

            // 12 jeux d'overlays de plateaux européens.
            for (std::size_t board = 0; board < 12; ++board)
            {
                for (const auto resolution :
                    {TextureResolution::Pixels128,
                     TextureResolution::Pixels256})
                {
                    appendCorpusAssets(
                        result,
                        indexedTextureDirectory(
                            "Boards", "Board", board, "Medium"),
                        europeanBoardOverlayTextureNames(
                            BoardMeshKind::ClassicMedium,
                            resolution),
                        TextureRole::BoardOverlay,
                        TextureLocation::SelectedBoard,
                        resolution,
                        8
                    );
                    appendCorpusAssets(
                        result,
                        indexedTextureDirectory(
                            "Boards", "Board", board, "High"),
                        europeanBoardOverlayTextureNames(
                            BoardMeshKind::ClassicHigh,
                            resolution),
                        TextureRole::BoardOverlay,
                        TextureLocation::SelectedBoard,
                        resolution,
                        8
                    );
                }
            }

            // City00 ne contient que les sept substitutions classiques 256.
            appendCorpusAssets(
                result,
                "Cities/City00/Medium",
                ClassicMediumNames256,
                TextureRole::CityName,
                TextureLocation::SelectedCityNames,
                TextureResolution::Pixels256,
                8
            );
            appendCorpusAssets(
                result,
                "Cities/City00/High",
                ClassicHighNames256,
                TextureRole::CityName,
                TextureLocation::SelectedCityNames,
                TextureResolution::Pixels256,
                8
            );

            // Dix villes : 6 photos, 4 noms medium et 3 noms high dans les
            // deux résolutions. Les photos sont les seuls BMP 24 bits.
            for (std::size_t city = 1; city <= 10; ++city)
            {
                for (const auto resolution :
                    {TextureResolution::Pixels128,
                     TextureResolution::Pixels256})
                {
                    appendCorpusAssets(
                        result,
                        indexedTextureDirectory(
                            "Cities", "City", city, "Photos"),
                        cityPhotoTextureNames(resolution),
                        TextureRole::CityPhoto,
                        TextureLocation::SelectedCityPhotos,
                        resolution,
                        24
                    );
                    appendCorpusAssets(
                        result,
                        indexedTextureDirectory(
                            "Cities", "City", city, "Medium"),
                        usaCityNameTextureNames(
                            BoardMeshKind::CityMedium,
                            resolution),
                        TextureRole::CityName,
                        TextureLocation::SelectedCityNames,
                        resolution,
                        8
                    );

                    const auto highNames =
                        resolution == TextureResolution::Pixels128
                        ? std::span<const std::string_view>
                            {PhysicalCityHighNames128}
                        : usaCityNameTextureNames(
                            BoardMeshKind::CityHigh,
                            resolution);

                    appendCorpusAssets(
                        result,
                        indexedTextureDirectory(
                            "Cities", "City", city, "High"),
                        highNames,
                        TextureRole::CityName,
                        TextureLocation::SelectedCityNames,
                        resolution,
                        8
                    );
                }
            }

            for (const auto resolution :
                {TextureResolution::Pixels128,
                 TextureResolution::Pixels256})
            {
                appendCorpusAssets(
                    result,
                    "Cities/Common/Medium",
                    commonTextureNames(
                        BoardMeshKind::ClassicMedium,
                        resolution),
                    TextureRole::Common,
                    TextureLocation::SharedCityCommon,
                    resolution,
                    8
                );
                appendCorpusAssets(
                    result,
                    "Cities/Common/High",
                    commonTextureNames(
                        BoardMeshKind::ClassicHigh,
                        resolution),
                    TextureRole::Common,
                    TextureLocation::SharedCityCommon,
                    resolution,
                    8
                );
            }

            // Treize systèmes monétaires, chacun avec medium/high et les
            // overlays GO correspondants.
            for (std::size_t currency = 0; currency < 13; ++currency)
            {
                for (const auto resolution :
                    {TextureResolution::Pixels128,
                     TextureResolution::Pixels256})
                {
                    const auto mediumDirectory = indexedTextureDirectory(
                        "Currency", "Curr", currency, "Medium");
                    const auto highDirectory = indexedTextureDirectory(
                        "Currency", "Curr", currency, "High");

                    appendCorpusAssets(
                        result,
                        mediumDirectory,
                        europeanCurrencyTextureNames(
                            BoardMeshKind::ClassicMedium,
                            resolution),
                        TextureRole::Currency,
                        TextureLocation::Currency,
                        resolution,
                        8
                    );
                    appendCorpusAsset(
                        result,
                        mediumDirectory,
                        europeanGoOverlayTextureName(resolution),
                        TextureRole::GoCurrencyOverlay,
                        TextureLocation::Currency,
                        resolution,
                        8
                    );
                    appendCorpusAssets(
                        result,
                        highDirectory,
                        europeanCurrencyTextureNames(
                            BoardMeshKind::ClassicHigh,
                            resolution),
                        TextureRole::Currency,
                        TextureLocation::Currency,
                        resolution,
                        8
                    );
                    appendCorpusAsset(
                        result,
                        highDirectory,
                        europeanGoOverlayTextureName(resolution),
                        TextureRole::GoCurrencyOverlay,
                        TextureLocation::Currency,
                        resolution,
                        8
                    );
                }
            }

            // Neuf dossiers localisés (UK puis huit langues européennes).
            for (std::size_t language = 0; language < 9; ++language)
            {
                for (const auto resolution :
                    {TextureResolution::Pixels128,
                     TextureResolution::Pixels256})
                {
                    appendCorpusAssets(
                        result,
                        indexedTextureDirectory(
                            "Languages", "Lang", language, "Medium"),
                        europeanLanguageTextureNames(
                            BoardMeshKind::ClassicMedium,
                            resolution),
                        TextureRole::Language,
                        TextureLocation::Language,
                        resolution,
                        8
                    );
                    appendCorpusAssets(
                        result,
                        indexedTextureDirectory(
                            "Languages", "Lang", language, "High"),
                        europeanLanguageTextureNames(
                            BoardMeshKind::ClassicHigh,
                            resolution),
                        TextureRole::Language,
                        TextureLocation::Language,
                        resolution,
                        8
                    );
                }
            }

            return result;
        }();

        return manifest;
    }


    BoardTextureRecipeResult buildUsaTextureRecipe(
        BoardMeshKind mesh,
        TextureResolution resolution)
    {
        if (!isValidBoardMeshKind(mesh))
        {
            return std::unexpected(TextureCatalogError
            {
                .code = TextureCatalogErrorCode::InvalidBoardMeshKind,
                .rawValue = static_cast<std::uint32_t>(boardMeshTag(mesh)),
                .detail = "board mesh must be one of the four TexInfo HMD tags"
            });
        }

        if (!isValidTextureResolution(resolution))
        {
            return std::unexpected(TextureCatalogError
            {
                .code = TextureCatalogErrorCode::InvalidTextureResolution,
                .rawValue = static_cast<std::uint32_t>(resolution),
                .detail = "texture resolution must be exactly 128 or 256"
            });
        }

        BoardTextureRecipe recipe
        {
            .mesh = mesh,
            .resolution = resolution,
            .meshDataId = boardMeshDataId(mesh),
            .provision = TextureProvision::ExternalSubstitutions
        };

        if (!isCityMesh(mesh) &&
            resolution == TextureResolution::Pixels128)
        {
            recipe.provision = TextureProvision::EmbeddedInMesh;
            return recipe;
        }

        const auto coordinates = usaTextureCoordinates(mesh);
        const auto photos = isCityMesh(mesh)
            ? cityPhotoTextureNames(resolution)
            : std::span<const std::string_view>{};
        const auto names = usaCityNameTextureNames(mesh, resolution);
        const auto common = commonTextureNames(mesh, resolution);

        recipe.textures.reserve(
            photos.size() + names.size() + common.size()
        );

        appendTextureReferences(
            recipe,
            coordinates,
            0,
            photos,
            TextureRole::CityPhoto,
            TextureLocation::SelectedCityPhotos
        );
        appendTextureReferences(
            recipe,
            coordinates,
            photos.size(),
            names,
            TextureRole::CityName,
            TextureLocation::SelectedCityNames
        );
        appendTextureReferences(
            recipe,
            coordinates,
            photos.size() + names.size(),
            common,
            TextureRole::Common,
            TextureLocation::SharedCityCommon
        );

        return recipe;
    }


    BoardTextureRecipeResult buildEuropeanTextureRecipe(
        BoardMeshKind mesh,
        TextureResolution resolution)
    {
        if (!isValidBoardMeshKind(mesh))
        {
            return std::unexpected(TextureCatalogError
            {
                .code = TextureCatalogErrorCode::InvalidBoardMeshKind,
                .rawValue = static_cast<std::uint32_t>(boardMeshTag(mesh)),
                .detail = "board mesh must be one of the four TexInfo HMD tags"
            });
        }

        if (!isValidTextureResolution(resolution))
        {
            return std::unexpected(TextureCatalogError
            {
                .code = TextureCatalogErrorCode::InvalidTextureResolution,
                .rawValue = static_cast<std::uint32_t>(resolution),
                .detail = "texture resolution must be exactly 128 or 256"
            });
        }

        BoardTextureRecipe recipe
        {
            .mesh = mesh,
            .resolution = resolution,
            .meshDataId = boardMeshDataId(mesh),
            .provision = TextureProvision::ExternalSubstitutions
        };

        const auto coordinates = europeanTextureCoordinates(mesh);
        const auto photos = isCityMesh(mesh)
            ? cityPhotoTextureNames(resolution)
            : std::span<const std::string_view>{};
        const auto names = isCityMesh(mesh)
            ? usaCityNameTextureNames(mesh, resolution)
            : std::span<const std::string_view>{};
        const auto languages =
            europeanLanguageTextureNames(mesh, resolution);
        const auto boardOverlays =
            europeanBoardOverlayTextureNames(mesh, resolution);
        const auto currencies =
            europeanCurrencyTextureNames(mesh, resolution);

        // Dans le code original, les textures de langue déjà remplacées par
        // les noms de ville sont sautées : indices 4..6 (medium) ou 3..10
        // (high) seulement pour un mesh de ville.
        const std::size_t firstLanguage = names.size();
        const std::size_t languageCount = languages.size() - firstLanguage;

        recipe.textures.reserve(
            photos.size() + names.size() + languageCount + currencies.size()
        );

        appendTextureReferences(
            recipe,
            coordinates,
            0,
            photos,
            TextureRole::CityPhoto,
            TextureLocation::SelectedCityPhotos
        );
        appendTextureReferences(
            recipe,
            coordinates,
            photos.size(),
            names,
            TextureRole::CityName,
            TextureLocation::SelectedCityNames
        );

        const std::size_t languageCoordinateOffset = photos.size();

        for (
            std::size_t languageIndex = firstLanguage;
            languageIndex < languages.size();
            ++languageIndex)
        {
            TextureReference reference
            {
                .coordinate = coordinates[
                    languageCoordinateOffset + languageIndex],
                .resolution = resolution,
                .role = TextureRole::Language,
                .location = TextureLocation::Language,
                .fileName = languages[languageIndex]
            };

            // Cette condition volontairement exprimée avec l'indice global
            // reproduit le caller historique. Elle applique les overlays de
            // plateau au classique et aucun aux meshes de ville.
            if (languageIndex < boardOverlays.size())
            {
                reference.overlay = TextureOverlayReference
                {
                    .role = TextureRole::BoardOverlay,
                    .location = TextureLocation::SelectedBoard,
                    .fileName = boardOverlays[
                        languageIndex - languageCoordinateOffset]
                };
            }
            else if (languageIndex == languages.size() - 1)
            {
                reference.overlay = TextureOverlayReference
                {
                    .role = TextureRole::GoCurrencyOverlay,
                    .location = TextureLocation::Currency,
                    .fileName = europeanGoOverlayTextureName(resolution)
                };
            }

            recipe.textures.push_back(reference);
        }

        appendTextureReferences(
            recipe,
            coordinates,
            photos.size() + languages.size(),
            currencies,
            TextureRole::Currency,
            TextureLocation::Currency
        );

        return recipe;
    }
}
