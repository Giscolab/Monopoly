#include "PieceJailPlan.hpp"
#include "PiecePlacement.hpp"

#include <cmath>
#include <iostream>

namespace
{
    int failures{};

    void expect(bool condition, const char* message)
    {
        if (condition) std::cout << "[PASS] " << message << '\n';
        else { std::cerr << "[FAIL] " << message << '\n'; ++failures; }
    }

    bool near(float a, float b, float epsilon = 0.001F)
    { return std::fabs(a - b) <= epsilon; }

    void testOutboundRoutes()
    {
        using namespace monopoly::pieces;
        const auto side0 = planPaddyToToken(4, 100);
        expect(side0 && side0->path.used == 2,
            "side-0 outbound builds the two historical Bezier segments");
        expect(side0 && side0->path.endTick == 190,
            "side-0 outbound duration is 60 + 5*(10-square)");
        expect(side0 && side0->camera == pickCameraFor15Squares(4),
            "side-0 early squares use the 15-square camera selector");

        const auto side1 = planPaddyToToken(16, 200);
        expect(side1 && side1->path.used == 3,
            "side-1 outbound builds approach, pass-token and smooth finish");
        expect(side1 && side1->path.endTick == 290,
            "side-1 outbound duration is 60 + 5*(square-10)");
        expect(side1 && side1->camera == BoardCameraView::FifteenTiles03,
            "side-1 far squares use VIEW2D30_15TILES03");

        const auto side2 = planPaddyToToken(27, 300);
        expect(side2 && side2->path.used == 4,
            "side-2 outbound preserves all four Bezier segments");
        expect(side2 && side2->path.endTick == 445,
            "side-2 outbound duration follows the source distance formula");
        expect(side2 && side2->camera == BoardCameraView::FifteenTiles06,
            "side-2 far squares use VIEW2D33_15TILES06");

        const auto side3a = planPaddyToToken(30, 400, 0);
        const auto side3b = planPaddyToToken(30, 400, 1);
        expect(side3a && side3a->path.used == 2 && side3b && side3b->path.used == 2,
            "side-3 outbound uses the two source Bezier segments");
        expect(side3a && side3a->camera == BoardCameraView::CornerGoToJail &&
            side3b && side3b->camera == BoardCameraView::FifteenTiles09,
            "square 30 preserves the source rand()%2 camera branch");

        const auto jail = planPaddyToToken(10, 500);
        expect(jail && jail->skipMotion && jail->path.used == 0,
            "square 10 short-circuits the paddy travel path");
        expect(jail && jail->path.startTick == jail->path.endTick,
            "square 10 advertises zero travel duration");
    }

    void testReturnRoutes()
    {
        using namespace monopoly::pieces;
        const InterpolationVec3 start{100.0F, 0.0F, 200.0F};

        const auto side0 = planPaddyToJail(4, start, 1000);
        expect(side0 && side0->path.used == 1 &&
            side0->camera == BoardCameraView::CornerJail,
            "side-0 return uses one Bezier and the jail corner camera");
        expect(side0 && side0->path.endTick == 1070,
            "side-0 return duration is 40 + 5*(10-square)");

        const auto side1 = planPaddyToJail(12, start, 1100);
        expect(side1 && side1->path.used == 1 &&
            side1->camera == BoardCameraView::ThreeTiles04,
            "near side-1 return uses VIEW2D07_3TILES04");

        const auto side2 = planPaddyToJail(26, start, 1200);
        expect(side2 && side2->path.used == 3,
            "side-2 return preserves the three source Bezier segments");
        expect(side2 && side2->camera == BoardCameraView::FifteenTiles03,
            "side-2 return keeps VIEW2D30_15TILES03");
        expect(side2 && side2->path.lengths[2] > 0.0F,
            "side-2 return keeps the slowed final-segment length hack");

        const auto side3 = planPaddyToJail(36, start, 1300);
        expect(side3 && side3->path.used == 2,
            "side-3 return uses the GO and jail corner segments");
        expect(side3 && side3->camera == BoardCameraView::FiveTiles03,
            "late side-3 return preserves VIEW2D22_5TILES03");

        const auto drop = tokenOrientation(11);
        expect(side3 && drop && near(side3->path.segments[1][3].x, drop->x) &&
            near(side3->path.segments[1][3].z, drop->z),
            "all return routes terminate at the historical square-11 drop location");
    }
}

int main()
{
    testOutboundRoutes();
    testReturnRoutes();
    if (failures != 0)
        std::cerr << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
