#include "PiecePlacement.hpp"

#include <cmath>
#include <iostream>
#include <numbers>
#include <string_view>

namespace
{
    int failures{};
    bool near(float a, float b) { return std::fabs(a - b) < 0.0001F; }
    void expect(bool value, std::string_view text)
    {
        std::cout << (value ? "[PASS] " : "[FAIL] ") << text << '\n';
        if (!value) ++failures;
    }

    void testBaseOrientation()
    {
        using monopoly::pieces::tokenOrientation;
        const auto go = tokenOrientation(0);
        expect(go && near(go->x, 30.0F) && near(go->y, 0.0F) &&
            near(go->z, 45.5F) && near(go->yaw, 0.0F),
            "GO uses side-1 token offsets and zero yaw");
        const auto visiting = tokenOrientation(10);
        expect(visiting && near(visiting->x, 45.5F) && near(visiting->z, 456.0F) &&
            near(visiting->yaw, std::numbers::pi_v<float> / 2.0F),
            "Just Visiting uses side-2 offsets and +90 degree yaw");
        const auto freeParking = tokenOrientation(20);
        expect(freeParking && near(freeParking->x, 456.0F) && near(freeParking->z, 440.5F) &&
            near(freeParking->yaw, std::numbers::pi_v<float>),
            "Free Parking uses side-3 offsets and 180 degree yaw");
        const auto goToJail = tokenOrientation(30);
        expect(goToJail && near(goToJail->x, 440.5F) && near(goToJail->z, 30.0F) &&
            near(goToJail->yaw, -std::numbers::pi_v<float> / 2.0F),
            "Go To Jail uses side-4 offsets and -90 degree yaw");
        const auto jail = tokenOrientation(40);
        expect(jail && near(jail->x, visiting->x) && near(jail->z, visiting->z) &&
            near(jail->yaw, visiting->yaw),
            "In Jail reuses the Just Visiting base center before resting offsets");
        const auto offBoard = tokenOrientation(41);
        expect(offBoard && near(offBoard->x, 83.5F) && near(offBoard->z, 134.0F) &&
            near(offBoard->yaw, -std::numbers::pi_v<float> / 2.0F),
            "Off-board retains the historical fourth-side base formula");
        expect(!tokenOrientation(42), "out-of-range square is rejected instead of indexing past legacy data");
    }

    void testRestingOffsets()
    {
        using monopoly::pieces::tokenRestingOrientation;
        const auto side1Gun = tokenRestingOrientation(1, 0, 0);
        expect(side1Gun && near(side1Gun->x, 9.0F) && near(side1Gun->z, 95.0F) &&
            near(side1Gun->yaw, std::numbers::pi_v<float> / 2.0F),
            "property side 1 applies direct resting offset and gun rotation");
        const auto side1Hat = tokenRestingOrientation(1, 0, 3);
        expect(side1Hat && near(side1Hat->x, 9.0F) && near(side1Hat->z, 95.0F) &&
            near(side1Hat->yaw, 0.0F),
            "hat shares position offset but not the six-token extra rotation");
        const auto side2Gun = tokenRestingOrientation(11, 0, 0);
        expect(side2Gun && near(side2Gun->x, 95.0F) && near(side2Gun->z, 477.0F) &&
            near(side2Gun->yaw, std::numbers::pi_v<float>),
            "property side 2 rotates resting x/z offsets with the board edge");
        const auto side3Gun = tokenRestingOrientation(21, 0, 0);
        expect(side3Gun && near(side3Gun->x, 477.0F) && near(side3Gun->z, 391.0F) &&
            near(side3Gun->yaw, 3.0F * std::numbers::pi_v<float> / 2.0F),
            "property side 3 mirrors both resting offsets");
        const auto side4Gun = tokenRestingOrientation(31, 0, 0);
        expect(side4Gun && near(side4Gun->x, 391.0F) && near(side4Gun->z, 9.0F) &&
            near(side4Gun->yaw, 0.0F),
            "property side 4 rotates resting offsets back toward GO");
    }

    void testCornerAndSpecialResting()
    {
        using monopoly::pieces::tokenRestingOrientation;
        const auto visiting = tokenRestingOrientation(10, 0, 0);
        expect(visiting && near(visiting->x, 52.5F) && near(visiting->z, 478.0F) &&
            near(visiting->yaw, 3.0F * std::numbers::pi_v<float> / 4.0F),
            "Just Visiting uses its dedicated resting table");
        const auto jail = tokenRestingOrientation(40, 0, 0);
        expect(jail && near(jail->x, 54.5F) && near(jail->z, 431.0F) &&
            near(jail->yaw, -std::numbers::pi_v<float> / 4.0F),
            "In Jail switches to the dedicated jail offsets after the shared base pose");
        const auto freeParking = tokenRestingOrientation(20, 0, 0);
        expect(freeParking && near(freeParking->x, 478.0F) && near(freeParking->z, 433.5F) &&
            near(freeParking->yaw, 5.0F * std::numbers::pi_v<float> / 4.0F),
            "Free Parking mirrors GO/FP offsets and keeps its token rotation");
        const auto offBoard = tokenRestingOrientation(41, 0, 0);
        expect(offBoard && near(offBoard->x, 83.5F) && near(offBoard->z, 134.0F) &&
            near(offBoard->yaw, -std::numbers::pi_v<float> / 4.0F),
            "Off-board applies no resting translation but preserves default-category token rotation");
        expect(!tokenRestingOrientation(0, 6, 0),
            "resting position outside the six-entry legacy table is rejected");
    }
    void testBuildingPlacement()
    {
        using monopoly::pieces::hotelPosition;
        using monopoly::pieces::housePosition;
        const auto hotelGo = hotelPosition(0);
        expect(hotelGo && near(hotelGo->x, 58.25F) && near(hotelGo->z, 45.5F) && near(hotelGo->yaw, 0.0F),
            "hotel on side 1 uses the historical swatch offset");
        const auto hotelVisiting = hotelPosition(10);
        expect(hotelVisiting && near(hotelVisiting->x, 45.5F) && near(hotelVisiting->z, 427.75F) &&
            near(hotelVisiting->yaw, std::numbers::pi_v<float> / 2.0F),
            "hotel on side 2 rotates the same offsets");
        const auto hotelJail = hotelPosition(40);
        expect(hotelJail && near(hotelJail->x, hotelVisiting->x) && near(hotelJail->z, hotelVisiting->z),
            "hotel In Jail reuses the Just Visiting center exactly as source");
        const auto house0 = housePosition(1, 0);
        const auto house3 = housePosition(1, 3);
        expect(house0 && house3 && near(house0->x, 58.25F) && near(house0->z, 99.1F) &&
            near(house3->z, 70.9F), "house slots reproduce HOUSE0..HOUSE3 X offsets");
        expect(!housePosition(1, 4), "invalid fifth house slot is rejected instead of reproducing legacy UB");
        expect(!hotelPosition(42), "building placement rejects a square beyond BoardGeometry");
    }}

int main()
{
    testBaseOrientation();
    testRestingOffsets();
    testCornerAndSpecialResting();
    testBuildingPlacement();
    std::cout << (failures ? "Piece placement tests FAILED\n" : "Piece placement tests passed\n");
    return failures ? 1 : 0;
}
