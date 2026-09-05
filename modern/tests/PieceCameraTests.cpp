#include "PieceCamera.hpp"
#include "TextureCatalog.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace
{
    using monopoly::pieces::BoardCameraView;
    using monopoly::pieces::pickCameraFor3Squares;
    using monopoly::pieces::pickCameraFor5Squares;
    using monopoly::pieces::pickCameraFor15Squares;
    using monopoly::pieces::pickGoodCamera;
    int failures{};

    void expect(bool value, std::string_view text)
    {
        std::cout << (value ? "[PASS] " : "[FAIL] ") << text << '\n';
        if (!value) ++failures;
    }

    std::uint8_t raw(BoardCameraView view)
    {
        return static_cast<std::uint8_t>(view);
    }

    void testCameraTables()
    {
        constexpr std::array<std::uint8_t, 42> expected3{{
            15, 3,3,3, 4,4,4, 5,5,5, 16, 6,6,6, 7,7,7, 8,8,8, 17,
            9,9,9, 10,10,10, 11,11,11, 18, 12,12,12, 13,13,13, 14,14,14, 16,15}};
        constexpr std::array<std::uint8_t, 42> expected5{{
            19,19,19,19,19,19, 20,20,20,20, 21,21,21,21,21,21,
            22,22,22,22, 23,23,23,23,23,23, 24,24,24,24,
            25,25,25,25,25,25, 26,26,26,26, 16,15}};
        constexpr std::array<std::uint8_t, 42> expected15{{
            27,27,27,27, 28,28,28, 29,29,29, 30,30,30,30,
            31,31,31, 32,32,32, 33,33,33,33, 34,34,34,
            35,35,35, 36,36,36,36, 37,37,37, 38,38,38, 16,15}};
        bool all = true;
        for (std::int32_t square = 0; square != 42; ++square)
            all = all && raw(pickCameraFor3Squares(square)) == expected3[square] &&
                raw(pickCameraFor5Squares(square)) == expected5[square] &&
                raw(pickCameraFor15Squares(square)) == expected15[square];
        expect(all, "3/5/15-square camera tables match every legacy square including jail/off-board");
        expect(raw(pickCameraFor3Squares(-1)) == 15 && raw(pickCameraFor15Squares(99)) == 15,
            "invalid camera square falls back to the historical GO corner view");
    }

    void testGoodCameraSelection()
    {
        expect(pickGoodCamera(0, 0) == BoardCameraView::CornerGo &&
            pickGoodCamera(10, 0) == BoardCameraView::CornerJail &&
            pickGoodCamera(20, 0) == BoardCameraView::CornerFreeParking &&
            pickGoodCamera(30, 0) == BoardCameraView::CornerGoToJail,
            "zero-distance CX0 moves hold the exact corner camera");
        expect(pickGoodCamera(8, 3) == BoardCameraView::CornerJail,
            "short run crossing a corner selects that corner view");
        expect(pickGoodCamera(5, 9) == BoardCameraView::FifteenTiles02,
            "distance >= 9 uses the 15-square view at the start square");
        expect(pickGoodCamera(1, 2) == BoardCameraView::ThreeTiles01,
            "destination in first three positions uses the final 3-square view");
        expect(pickGoodCamera(1, 5) == BoardCameraView::FiveTiles01,
            "ordinary straight run uses the 5-square view one square before destination");
    }

    void testTextureIndexContract()
    {
        const auto names = monopoly::data::twoDimensionalBoardTextureNames();
        expect(names.size() == raw(BoardCameraView::Count),
            "camera enum count matches the 39 catalogued 2DVIEW textures");
        expect(names[raw(BoardCameraView::ThreeTiles01)] == "2DVIEW04.BMP" &&
            names[raw(BoardCameraView::CornerJail)] == "2DVIEW17.BMP" &&
            names[raw(BoardCameraView::FifteenTiles12)] == "2DVIEW39.BMP",
            "camera enum indices address the same filenames as UDBoard VIEWS_2D");
    }

}

int main()
{
    testCameraTables();
    testGoodCameraSelection();
    testTextureIndexContract();
    std::cout << (failures ? "Piece camera tests FAILED\n" : "Piece camera tests passed\n");
    return failures ? 1 : 0;
}
