#include "PieceCamera.hpp"

#include <array>

namespace monopoly::pieces
{
    namespace
    {
        using enum BoardCameraView;

        constexpr std::array<BoardCameraView, 42> Camera3{{
            CornerGo,
            ThreeTiles01, ThreeTiles01, ThreeTiles01,
            ThreeTiles02, ThreeTiles02, ThreeTiles02,
            ThreeTiles03, ThreeTiles03, ThreeTiles03,
            CornerJail,
            ThreeTiles04, ThreeTiles04, ThreeTiles04,
            ThreeTiles05, ThreeTiles05, ThreeTiles05,
            ThreeTiles06, ThreeTiles06, ThreeTiles06,
            CornerFreeParking,
            ThreeTiles07, ThreeTiles07, ThreeTiles07,
            ThreeTiles08, ThreeTiles08, ThreeTiles08,
            ThreeTiles09, ThreeTiles09, ThreeTiles09,
            CornerGoToJail,
            ThreeTiles10, ThreeTiles10, ThreeTiles10,
            ThreeTiles11, ThreeTiles11, ThreeTiles11,
            ThreeTiles12, ThreeTiles12, ThreeTiles12,
            CornerJail,
            CornerGo
        }};

        constexpr std::array<BoardCameraView, 42> Camera5{{
            FiveTiles01, FiveTiles01, FiveTiles01,
            FiveTiles01, FiveTiles01, FiveTiles01,
            FiveTiles02, FiveTiles02, FiveTiles02, FiveTiles02,
            FiveTiles03, FiveTiles03, FiveTiles03,
            FiveTiles03, FiveTiles03, FiveTiles03,
            FiveTiles04, FiveTiles04, FiveTiles04, FiveTiles04,
            FiveTiles05, FiveTiles05, FiveTiles05,
            FiveTiles05, FiveTiles05, FiveTiles05,
            FiveTiles06, FiveTiles06, FiveTiles06, FiveTiles06,
            FiveTiles07, FiveTiles07, FiveTiles07,
            FiveTiles07, FiveTiles07, FiveTiles07,
            FiveTiles08, FiveTiles08, FiveTiles08, FiveTiles08,
            CornerJail,
            CornerGo
        }};

        constexpr std::array<BoardCameraView, 42> Camera15{{
            FifteenTiles01, FifteenTiles01, FifteenTiles01, FifteenTiles01,
            FifteenTiles02, FifteenTiles02, FifteenTiles02,
            FifteenTiles03, FifteenTiles03, FifteenTiles03,
            FifteenTiles04, FifteenTiles04, FifteenTiles04, FifteenTiles04,
            FifteenTiles05, FifteenTiles05, FifteenTiles05,
            FifteenTiles06, FifteenTiles06, FifteenTiles06,
            FifteenTiles07, FifteenTiles07, FifteenTiles07, FifteenTiles07,
            FifteenTiles08, FifteenTiles08, FifteenTiles08,
            FifteenTiles09, FifteenTiles09, FifteenTiles09,
            FifteenTiles10, FifteenTiles10, FifteenTiles10, FifteenTiles10,
            FifteenTiles11, FifteenTiles11, FifteenTiles11,
            FifteenTiles12, FifteenTiles12, FifteenTiles12,
            CornerJail,
            CornerGo
        }};

        template <std::size_t Size>
        BoardCameraView tableLookup(const std::array<BoardCameraView, Size>& table,
            std::int32_t square) noexcept
        {
            if (square < 0 || static_cast<std::size_t>(square) >= table.size())
                return CornerGo;
            return table[static_cast<std::size_t>(square)];
        }
    }

    BoardCameraView pickCameraFor3Squares(std::int32_t square) noexcept
    {
        return tableLookup(Camera3, square);
    }

    BoardCameraView pickCameraFor5Squares(std::int32_t square) noexcept
    {
        return tableLookup(Camera5, square);
    }

    BoardCameraView pickCameraFor15Squares(std::int32_t square) noexcept
    {
        return tableLookup(Camera15, square);
    }

    BoardCameraView pickGoodCamera(
        std::int32_t square, std::int32_t distance) noexcept
    {
        std::int32_t distanceToCorner = 10 - (square % 10);
        if (distanceToCorner == 10 && distance == 0) distanceToCorner = 0;
        if (distance >= 9) return pickCameraFor15Squares(square);

        if (distanceToCorner < distance || distance == 0)
        {
            switch ((square + distanceToCorner) / 10)
            {
            case 1: return BoardCameraView::CornerJail;
            case 2: return BoardCameraView::CornerFreeParking;
            case 3: return BoardCameraView::CornerGoToJail;
            default: return BoardCameraView::CornerGo;
            }
        }

        const auto destination = (square + distance) % 40;
        if (destination <= 3)
            return pickCameraFor3Squares(destination);

        return pickCameraFor5Squares((square + distance - 1) % 40);
    }
}
