#pragma once

#include "DataBanks.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace monopoly::data
{
    // Les valeurs sont les tags HMD_board_* de Dat_Mon/dat_3d.h.
    enum class BoardMeshKind : DataTag
    {
        CityHigh = 0,
        CityMedium = 1,
        ClassicHigh = 2,
        ClassicMedium = 3
    };


    enum class TextureResolution : std::uint16_t
    {
        Pixels128 = 128,
        Pixels256 = 256
    };


    enum class TextureRole : std::uint8_t
    {
        CityPhoto,
        CityName,
        Common,
        Language,
        Currency,
        BoardOverlay,
        GoCurrencyOverlay,
        TwoDimensionalBoard
    };


    // Le même nom de fichier existe dans plusieurs répertoires du jeu.
    // Cette provenance fait donc partie de la référence de ressource.
    enum class TextureLocation : std::uint8_t
    {
        SelectedCityPhotos,
        SelectedCityNames,
        SharedCityCommon,
        Language,
        SelectedBoard,
        Currency,
        CustomBoard2D
    };


    struct TextureCoordinate
    {
        std::uint16_t x = 0;
        std::uint16_t y = 0;

        constexpr bool operator==(const TextureCoordinate&) const = default;
    };


    struct TextureDimensions
    {
        std::uint16_t width = 0;
        std::uint16_t height = 0;

        constexpr bool operator==(const TextureDimensions&) const = default;
    };


    struct TextureOverlayReference
    {
        TextureRole role = TextureRole::BoardOverlay;
        TextureLocation location = TextureLocation::SelectedBoard;
        std::string_view fileName;
    };


    struct TextureReference
    {
        TextureCoordinate coordinate;
        TextureResolution resolution = TextureResolution::Pixels128;
        TextureRole role = TextureRole::Common;
        TextureLocation location = TextureLocation::SharedCityCommon;
        std::string_view fileName;
        std::optional<TextureOverlayReference> overlay;
    };


    enum class TextureProvision : std::uint8_t
    {
        EmbeddedInMesh,
        ExternalSubstitutions
    };


    struct BoardTextureRecipe
    {
        BoardMeshKind mesh = BoardMeshKind::ClassicMedium;
        TextureResolution resolution = TextureResolution::Pixels128;
        DataId meshDataId = EmptyDataId;
        TextureProvision provision = TextureProvision::EmbeddedInMesh;
        std::vector<TextureReference> textures;
    };


    [[nodiscard]] constexpr DataTag boardMeshTag(
        BoardMeshKind mesh) noexcept
    {
        return static_cast<DataTag>(mesh);
    }


    [[nodiscard]] constexpr DataId boardMeshDataId(
        BoardMeshKind mesh) noexcept
    {
        return packDataId(LegacyGroupId::ThreeD, boardMeshTag(mesh));
    }


    [[nodiscard]] constexpr bool isCityMesh(
        BoardMeshKind mesh) noexcept
    {
        return
            mesh == BoardMeshKind::CityHigh ||
            mesh == BoardMeshKind::CityMedium;
    }


    [[nodiscard]] constexpr bool isHighDetailMesh(
        BoardMeshKind mesh) noexcept
    {
        return
            mesh == BoardMeshKind::CityHigh ||
            mesh == BoardMeshKind::ClassicHigh;
    }


    [[nodiscard]] constexpr TextureDimensions textureDimensions(
        TextureResolution resolution) noexcept
    {
        const auto side = static_cast<std::uint16_t>(resolution);
        return {side, side};
    }


    [[nodiscard]] std::span<const TextureCoordinate>
        usaTextureCoordinates(BoardMeshKind mesh) noexcept;

    [[nodiscard]] std::span<const TextureCoordinate>
        europeanTextureCoordinates(BoardMeshKind mesh) noexcept;

    [[nodiscard]] std::span<const std::string_view>
        cityPhotoTextureNames(TextureResolution resolution) noexcept;

    [[nodiscard]] std::span<const std::string_view>
        usaCityNameTextureNames(
            BoardMeshKind mesh,
            TextureResolution resolution) noexcept;

    [[nodiscard]] std::span<const std::string_view>
        commonTextureNames(
            BoardMeshKind mesh,
            TextureResolution resolution) noexcept;

    [[nodiscard]] std::span<const std::string_view>
        europeanLanguageTextureNames(
            BoardMeshKind mesh,
            TextureResolution resolution) noexcept;

    [[nodiscard]] std::span<const std::string_view>
        europeanBoardOverlayTextureNames(
            BoardMeshKind mesh,
            TextureResolution resolution) noexcept;

    [[nodiscard]] std::span<const std::string_view>
        europeanCurrencyTextureNames(
            BoardMeshKind mesh,
            TextureResolution resolution) noexcept;

    [[nodiscard]] std::string_view europeanGoOverlayTextureName(
        TextureResolution resolution) noexcept;

    [[nodiscard]] std::span<const std::string_view>
        twoDimensionalBoardTextureNames() noexcept;

    // Reproduit UDUTILS_SwitchToBoardUSA. Pour les meshes classiques en
    // 128 px, le code historique gardait les textures intégrées au HMD.
    [[nodiscard]] BoardTextureRecipe buildUsaTextureRecipe(
        BoardMeshKind mesh,
        TextureResolution resolution);

    // Reproduit la composition de UDUTILS_SwitchToBoardEURO. Les numéros de
    // langue, plateau et devise restent un contexte de résolution de chemin
    // fourni par le caller; les noms, l'ordre et les overlays sont complets.
    [[nodiscard]] BoardTextureRecipe buildEuropeanTextureRecipe(
        BoardMeshKind mesh,
        TextureResolution resolution);
}
