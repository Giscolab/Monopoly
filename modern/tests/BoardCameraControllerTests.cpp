#include "BoardCameraController.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

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

    bool near(float a, float b, float epsilon = 0.0005F)
    {
        return std::fabs(a - b) <= epsilon;
    }
}
namespace
{
    void testPresetAnchors()
    {
        using namespace monopoly;
        const auto& top = boardcamera::preset(
            pieces::BoardCameraView::TopDownSquare);
        expect(near(top.location[0], 242.9F) && near(top.location[1], 1200.0F) &&
            near(top.forward[1], -0.99999F),
            "top-down preset matches UDBoard CameraAngles2D[0]");

        const auto& jail = boardcamera::preset(
            pieces::BoardCameraView::CornerJail);
        expect(near(jail.location[0], -105.0F) && near(jail.location[2], 591.0F) &&
            near(jail.forward[2], -0.577096F),
            "jail corner preset matches historical table");

        const auto& last = boardcamera::preset(
            pieces::BoardCameraView::FifteenTiles12);
        expect(near(last.location[0], -164.0F) && near(last.location[1], 227.0F) &&
            near(last.forward[2], 0.717968F),
            "last fifteen-tile preset matches CameraAngles2D[38]");
    }
    void testPresetInterpolationAndForce()
    {
        using namespace monopoly;
        boardcamera::Controller controller;
        controller.reset(0);
        const auto start = controller.current();

        controller.requestPreset(pieces::BoardCameraView::TopDownSoccer, 10);
        expect(controller.waiting() && !controller.moving(),
            "3D preset is queued before TickActions starts it");
        auto update = controller.tick(10);
        expect(update.startedWaitingMove && controller.moving() &&
            controller.current() == start,
            "waiting preset starts without advancing interpolation in same tick");

        (void)controller.tick(47);
        expect(controller.current().location[0] < start.location[0] &&
            controller.current().location[0] > -420.0F,
            "preset interpolates between historical endpoints");
        update = controller.tick(85);
        expect(update.completedMove && !controller.moving() &&
            controller.current() == boardcamera::preset(
                pieces::BoardCameraView::TopDownSoccer),
            "preset completes exactly 75 ticks after actual start");

        controller.requestPreset(pieces::BoardCameraView::TopDownStarWars, 90, true);
        expect(!controller.waiting() && !controller.moving() &&
            controller.current() == boardcamera::preset(
                pieces::BoardCameraView::TopDownStarWars),
            "force interrupt applies preset immediately and clears waiting move");
    }
    void testWaitingReplacement()
    {
        using namespace monopoly;
        boardcamera::Controller controller;
        controller.reset(0);
        controller.requestPreset(pieces::BoardCameraView::TopDownSoccer, 0);
        (void)controller.tick(0);
        expect(controller.moving(), "first preset move is active");

        controller.requestPreset(pieces::BoardCameraView::TopDownStarWars, 10);
        controller.requestPreset(pieces::BoardCameraView::ThreeTiles01, 20);
        expect(controller.waiting(), "later standard camera replaces the single waiting slot");

        const auto update = controller.tick(75);
        expect(update.completedMove && update.startedWaitingMove &&
            controller.endCamera() == boardcamera::preset(
                pieces::BoardCameraView::ThreeTiles01),
            "replacement waiting camera starts on completion of current move");
        (void)controller.tick(150);
        expect(controller.current() == boardcamera::preset(
                pieces::BoardCameraView::ThreeTiles01),
            "replacement waiting camera completes normally");
    }
    void testDiceMoveUsesCurrentMoveDestination()
    {
        using namespace monopoly;
        boardcamera::Controller controller;
        controller.reset(0);
        controller.requestPreset(pieces::BoardCameraView::FifteenTiles12, 0);
        (void)controller.tick(0);
        (void)controller.tick(10);
        const auto interpolated = controller.current();
        const auto base = controller.endCamera();

        controller.requestPreset(pieces::BoardCameraView::CornerGo, 10);
        controller.requestDiceMove(10, 0);
        expect(controller.waiting(), "dice camera occupies the single waiting slot");

        const float ratioA = 0.30F;
        const float expectedX = base.location[0] * (1.0F - ratioA) + 240.0F * ratioA;
        expect(!near(expectedX,
                interpolated.location[0] * (1.0F - ratioA) + 240.0F * ratioA),
            "dice target distinguishes current position from current move destination");

        const auto update = controller.tick(75);
        expect(update.startedWaitingMove && near(controller.endCamera().location[0], expectedX),
            "dice target is calculated from CameraInterpolationData.End equivalent");
        expect(controller.endCamera() != boardcamera::preset(pieces::BoardCameraView::CornerGo),
            "dice camera replaces an already waiting standard preset");
    }
    void testDiceRandomRange()
    {
        using namespace monopoly;
        boardcamera::Controller low;
        boardcamera::Controller high;
        low.reset(0);
        high.reset(0);
        low.requestDiceMove(0, 0);
        high.requestDiceMove(0, 13);
        (void)low.tick(0);
        (void)high.tick(0);
        expect(low.endCamera().location != high.endCamera().location,
            "rand modulo fourteen changes dice camera destination");
        expect(high.endCamera().location[1] < low.endCamera().location[1],
            "larger dice ratio moves high top-down camera farther toward y=300");
    }
}

int main()
{
    std::cout << "Monopoly board camera controller tests\n"
              << "======================================\n";
    testPresetAnchors();
    testPresetInterpolationAndForce();
    testWaitingReplacement();
    testDiceMoveUsesCurrentMoveDestination();
    testDiceRandomRange();

    if (failures != 0)
    {
        std::cerr << failures << " board camera controller test(s) failed.\n";
        return 1;
    }
    std::cout << "All board camera controller tests passed.\n";
    return 0;
}
