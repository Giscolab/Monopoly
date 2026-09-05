#pragma once

#include <cstdint>

namespace monopoly::pieces
{
    // Source/monopoly/UDBoard.h::VIEWS_2D. Values are zero-based indices
    // into TextureCatalog::twoDimensionalBoardTextureNames().
    enum class BoardCameraView : std::uint8_t
    {
        TopDownSquare = 0,
        TopDownSoccer,
        TopDownStarWars,
        ThreeTiles01,
        ThreeTiles02,
        ThreeTiles03,
        ThreeTiles04,
        ThreeTiles05,
        ThreeTiles06,
        ThreeTiles07,
        ThreeTiles08,
        ThreeTiles09,
        ThreeTiles10,
        ThreeTiles11,
        ThreeTiles12,
        CornerGo,
        CornerJail,
        CornerFreeParking,
        CornerGoToJail,
        FiveTiles01,
        FiveTiles02,
        FiveTiles03,
        FiveTiles04,
        FiveTiles05,
        FiveTiles06,
        FiveTiles07,
        FiveTiles08,
        FifteenTiles01,
        FifteenTiles02,
        FifteenTiles03,
        FifteenTiles04,
        FifteenTiles05,
        FifteenTiles06,
        FifteenTiles07,
        FifteenTiles08,
        FifteenTiles09,
        FifteenTiles10,
        FifteenTiles11,
        FifteenTiles12,
        Count
    };

    [[nodiscard]] BoardCameraView pickCameraFor3Squares(std::int32_t square) noexcept;
    [[nodiscard]] BoardCameraView pickCameraFor5Squares(std::int32_t square) noexcept;
    [[nodiscard]] BoardCameraView pickCameraFor15Squares(std::int32_t square) noexcept;
    [[nodiscard]] BoardCameraView pickGoodCamera(
        std::int32_t square, std::int32_t distance) noexcept;
}
