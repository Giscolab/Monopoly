#include "Display.hpp"
#include "BoardCameraController.hpp"
#include "IBar.hpp"
#include "PlayerSelection.hpp"

#include <array>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    int failures = 0;
    std::vector<std::string_view> calls;
    monopoly::display::Screen2D viewSeenByIBar =
        monopoly::display::Screen2D::Invalid;

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
}

// Doubles minimaux : cette suite lie la vraie machine Display.cpp sans
// entrainer les dependances RULE/SDL de ses deux consommateurs actuels.
namespace monopoly::ibar
{
    bool initialize()
    {
        calls.push_back("ibar.initialize");
        return true;
    }

    void shutdown()
    {
        calls.push_back("ibar.shutdown");
    }

    void tickActions(std::uint64_t)
    {
        calls.push_back("ibar.tick");
    }

    void show()
    {
        viewSeenByIBar =
            display::stateReadOnly().current2DView;
        calls.push_back("ibar.show");
    }
}

namespace monopoly::playerselection
{
    bool initialize()
    {
        calls.push_back("playerselection.initialize");
        return true;
    }

    void shutdown()
    {
        calls.push_back("playerselection.shutdown");
    }

    void show()
    {
        calls.push_back("playerselection.show");
    }
}

namespace
{
    void testEnumContract()
    {
        using namespace monopoly::display;

        expect(static_cast<std::uint8_t>(Screen2D::Black) == 0,
               "DISPLAY_SCREEN_Black == 0");
        expect(static_cast<std::uint8_t>(Screen2D::PlayerSelect) == 1,
               "DISPLAY_SCREEN_Pselect == 1");
        expect(static_cast<std::uint8_t>(Screen2D::PlayerSelectRules) == 2,
               "DISPLAY_SCREEN_PselectRules == 2");
        expect(static_cast<std::uint8_t>(Screen2D::Options) == 3,
               "DISPLAY_SCREEN_Options == 3");
        expect(static_cast<std::uint8_t>(Screen2D::Portfolio) == 4,
               "DISPLAY_SCREEN_PortfolioA == 4");
        expect(static_cast<std::uint8_t>(Screen2D::Main) == 5,
               "DISPLAY_SCREEN_MainA == 5");
        expect(static_cast<std::uint8_t>(Screen2D::Auction) == 6,
               "DISPLAY_SCREEN_AuctionA == 6");
        expect(static_cast<std::uint8_t>(Screen2D::Trade) == 7,
               "DISPLAY_SCREEN_TradeA == 7");

        expect(static_cast<std::uint8_t>(Viewport3D::Main) == 0,
               "VIEWPORT_MAIN == 0");
        expect(static_cast<std::uint8_t>(Viewport3D::Status) == 1,
               "VIEWPORT_STATUS == 1");
        expect(static_cast<std::uint8_t>(Viewport3D::Trade) == 2,
               "VIEWPORT_TRADE == 2");
        expect(static_cast<std::uint8_t>(Viewport3D::Off) == 3,
               "VIEWPORT_OFF == 3");
        expect(Board3DPriority == 90,
               "DISPLAY_Board3dPriority == 90");
        expect(isBoardVisible(Screen2D::Main) &&
               isBoardVisible(Screen2D::Trade) &&
               isBoardVisible(Screen2D::Portfolio),
               "Main, Trade and Portfolio keep the historical board visible");
        expect(!isBoardVisible(Screen2D::PlayerSelect) &&
               !isBoardVisible(Screen2D::Options) &&
               !isBoardVisible(Screen2D::Auction) &&
               !isBoardVisible(Screen2D::Black),
               "non-board screens do not request the historical board");

        expect(static_cast<std::uint8_t>(PlayerSetupPhase::EnterName) == 4,
               "UDPSEL_ENTERNAME == 4");
        expect(static_cast<std::uint8_t>(PlayerSetupPhase::SelectToken) == 5,
               "UDPSEL_SELECTTOKEN == 5");
        expect(static_cast<std::uint8_t>(PlayerSetupPhase::StartAddRemove) == 6,
               "UDPSEL_STARTADDREMOVE == 6");
    }

    bool near(float a, float b)
    {
        return std::fabs(a - b) < 0.001F;
    }

    bool sameCamera(const monopoly::engine::World3DCamera& a,
        const monopoly::engine::World3DCamera& b)
    {
        for (std::size_t i = 0; i < 3; ++i)
            if (!near(a.location[i], b.location[i]) ||
                !near(a.forward[i], b.forward[i]) ||
                !near(a.up[i], b.up[i])) return false;
        return near(a.fieldOfView, b.fieldOfView) &&
            near(a.nearPlane, b.nearPlane) && near(a.farPlane, b.farPlane);
    }

    void testBoardCameraStateMachine()
    {
        using namespace monopoly;
        using namespace monopoly::display;

        expect(initialize(), "DISPLAY initializes for board-camera test");
        expect(sameCamera(stateReadOnly().worldCamera,
            boardcamera::preset(pieces::BoardCameraView::TopDownSquare)),
            "camera controller resets to historical camera 0");

        showAll2();
        expect(stateReadOnly().currentBoardCamera ==
            pieces::BoardCameraView::TopDownSoccer,
            "first UDBoard show validates desired Soccer camera");
        expect(sameCamera(stateReadOnly().worldCamera,
            boardcamera::preset(pieces::BoardCameraView::TopDownSoccer)),
            "camera is forced instantly while 3D board is off");

        setBackdrop(Screen2D::Main);
        state().game3DOn = false;
        showAll2();
        expect(!stateReadOnly().board3DOn,
            "Main keeps board3DOn off when game3DOn disables the 3D board");
        state().game3DOn = true;
        showAll2();
        expect(stateReadOnly().board3DOn,
            "Main activates board3DOn when game3DOn is enabled");

        state().desiredBoardCamera = pieces::BoardCameraView::FifteenTiles12;
        showAll2();
        expect(stateReadOnly().currentBoardCamera ==
            pieces::BoardCameraView::FifteenTiles12,
            "UDBoard show records changed desired camera");
        expect(sameCamera(stateReadOnly().worldCamera,
            boardcamera::preset(pieces::BoardCameraView::TopDownSoccer)),
            "3D camera request is waiting, not teleported");

        tickActions(1);
        expect(sameCamera(stateReadOnly().worldCamera,
            boardcamera::preset(pieces::BoardCameraView::TopDownSoccer)),
            "camera waits at preset start for first chained tick");
        tickActions(74);
        expect(!sameCamera(stateReadOnly().worldCamera,
            boardcamera::preset(pieces::BoardCameraView::FifteenTiles12)),
            "camera remains in flight before 75 elapsed ticks");
        tickActions(1);
        expect(sameCamera(stateReadOnly().worldCamera,
            boardcamera::preset(pieces::BoardCameraView::FifteenTiles12)),
            "camera reaches preset after exactly 75 elapsed ticks");

        beginDiceCameraOverride(static_cast<std::uint8_t>(0));
        expect(stateReadOnly().diceCameraControlActive &&
            stateReadOnly().desiredCameraInvalidatedLock &&
            !stateReadOnly().desiredCameraClearToValidate,
            "dice camera override invalidates standard preset");
        tickActions(1);
        releaseDiceCameraOverride();
        expect(stateReadOnly().desiredCameraClearToValidate,
            "dice camera release requests standard-camera revalidation");
        showAll2();
        expect(!stateReadOnly().desiredCameraInvalidatedLock &&
            !stateReadOnly().desiredCameraClearToValidate,
            "next UDBoard show consumes camera revalidation flags");

        tickActions(75);
        expect(!sameCamera(stateReadOnly().worldCamera,
            boardcamera::preset(pieces::BoardCameraView::FifteenTiles12)),
            "dice move completes before queued preset restart");
        tickActions(75);
        expect(sameCamera(stateReadOnly().worldCamera,
            boardcamera::preset(pieces::BoardCameraView::FifteenTiles12)),
            "revalidated preset returns through a second 75-tick move");

        beginDiceCameraOverride(std::nullopt);
        endDiceCameraOverrideEarly();
        expect(!stateReadOnly().diceCameraControlActive &&
            stateReadOnly().desiredCameraInvalidatedLock &&
            !stateReadOnly().desiredCameraClearToValidate,
            "early IBar-style exit preserves invalidated camera flag");
        cancelDiceCameraOverride();
        showAll2();

        shutdown();
    }

    void testManualMouseBoardCamera()
    {
        using namespace monopoly;
        using namespace monopoly::display;

        expect(initialize(), "DISPLAY initializes for manual mouse camera test");
        showAll2();
        setBackdrop(Screen2D::Main);
        state().game3DOn = true;
        showAll2();
        expect(stateReadOnly().board3DOn,
            "manual camera test starts with visible 3D board");

        processBoardInput({uimsg::Type::MouseLeftDown, 400, 200});
        processBoardInput({uimsg::Type::MouseMoved, 900, 200, 10, 0});
        expect(!stateReadOnly().manualMouseCamLock,
            "left drag outside active viewport does not capture board camera");

        const auto presetBefore = stateReadOnly().worldCamera;
        processBoardInput({uimsg::Type::MouseMoved, 400, 200, 10, 0});
        expect(stateReadOnly().manualMouseCamLock &&
            stateReadOnly().manualMouseCamTime == 0,
            "left drag inside viewport acquires manual mouse camera lock");
        processBoardInput({uimsg::Type::MouseLeftUp, 400, 200});
        expect(stateReadOnly().manualMouseCamLock && !stateReadOnly().mouseLeftPressed,
            "releasing left button preserves timed manual camera lock");

        tickActions(boardcamera::BaseMoveTicks);
        expect(!sameCamera(stateReadOnly().worldCamera, presetBefore),
            "manual orbit reaches changed camera after 75 ticks");

        processBoardInput({uimsg::Type::MouseRightDown, 400, 200});
        processBoardInput({uimsg::Type::MouseLeftDown, 400, 200});
        const float yBeforeRightOrbit = stateReadOnly().worldCamera.location[1];
        processBoardInput({uimsg::Type::MouseMoved, 400, 200, 0, -25});
        expect(!near(stateReadOnly().worldCamera.location[1], yBeforeRightOrbit),
            "right-button left drag selects vertical orbit mode");
        processBoardInput({uimsg::Type::MouseRightUp, 400, 200});

        const float yBeforeControlOrbit = stateReadOnly().worldCamera.location[1];
        processBoardInput({uimsg::Type::MouseMoved, 400, 200, 0, 15,
            uimsg::MouseModifierControl});
        expect(!near(stateReadOnly().worldCamera.location[1], yBeforeControlOrbit),
            "Ctrl modifier selects the same vertical orbit path as right mouse");
        processBoardInput({uimsg::Type::MouseLeftUp, 400, 200});

        tickActions(60U * 20U);
        expect(stateReadOnly().manualMouseCamLock,
            "manual mouse camera remains locked at exactly 20 seconds");
        tickActions(1);
        expect(!stateReadOnly().manualMouseCamLock,
            "manual mouse camera releases strictly after 20 seconds");

        tickActions(1);
        tickActions(boardcamera::BaseMoveTicks);
        expect(sameCamera(stateReadOnly().worldCamera,
                boardcamera::preset(stateReadOnly().desiredBoardCamera)),
            "release revalidates and returns to desired standard camera preset");
        shutdown();
    }

    void testDisplayStateMachine()
    {
        using namespace monopoly::display;

        calls.clear();
        expect(initialize(), "DISPLAY initializes");
        expect(
            calls == std::vector<std::string_view>{
                "ibar.initialize", "playerselection.initialize" },
            "DISPLAY initializes IBar before PlayerSelection"
        );

        expect(stateReadOnly().current2DView == Screen2D::Invalid,
               "current view starts invalid like legacy -1");
        expect(stateReadOnly().desired2DView == Screen2D::PlayerSelect,
               "PlayerSelect is the initial desired view");
        expect(stateReadOnly().viewportInUse == Viewport3D::Off,
               "initial 3D viewport is off");

        setBackdrop(Screen2D::Main);
        expect(stateReadOnly().desired2DView == Screen2D::Main,
               "setBackdrop changes desired view");
        expect(stateReadOnly().current2DView == Screen2D::Invalid,
               "setBackdrop does not commit current view");

        calls.clear();
        viewSeenByIBar = Screen2D::Invalid;
        showAll2();
        expect(stateReadOnly().current2DView == Screen2D::Main,
               "show commits desired view");
        expect(stateReadOnly().viewportInUse == Viewport3D::Main,
               "Main selects the main 3D viewport");
        expect(viewSeenByIBar == Screen2D::Main,
               "Board commits the desired view before IBar show");
        expect(
            calls == std::vector<std::string_view>{
                "ibar.show", "playerselection.show" },
            "IBar show precedes PlayerSelection show"
        );

        struct Mapping
        {
            Screen2D screen;
            Viewport3D viewport;
        };

        constexpr std::array mappings{
            Mapping{ Screen2D::Black, Viewport3D::Off },
            Mapping{ Screen2D::PlayerSelect, Viewport3D::Off },
            Mapping{ Screen2D::PlayerSelectRules, Viewport3D::Off },
            Mapping{ Screen2D::Options, Viewport3D::Off },
            Mapping{ Screen2D::Portfolio, Viewport3D::Status },
            Mapping{ Screen2D::Main, Viewport3D::Main },
            Mapping{ Screen2D::Auction, Viewport3D::Off },
            Mapping{ Screen2D::Trade, Viewport3D::Trade }
        };

        for (const Mapping mapping : mappings)
        {
            setBackdrop(mapping.screen);
            showAll2();
            expect(
                stateReadOnly().current2DView == mapping.screen &&
                stateReadOnly().viewportInUse == mapping.viewport,
                "legacy screen-to-viewport mapping"
            );
        }

        calls.clear();
        shutdown();
        expect(
            calls == std::vector<std::string_view>{
                "ibar.tick", "ibar.show", "playerselection.show",
                "ibar.shutdown", "playerselection.shutdown" },
            "shutdown shows Black then destroys IBar before PlayerSelection"
        );
        expect(!stateReadOnly().initialized,
               "DISPLAY shutdown clears lifecycle state");
    }
    void testBoardDemoMode()
    {
        using namespace monopoly;
        using namespace monopoly::display;

        std::srand(1);
        expect(initialize(), "DISPLAY initializes for board demo test");
        setBackdrop(Screen2D::Main);
        state().game3DOn = true;
        state().desiredBoardCamera = pieces::BoardCameraView::TopDownSoccer;
        showAll2();
        expect(stateReadOnly().board3DOn,
            "demo test starts with 3D board visible");

        tickActions(90U * 60U);
        expect(!stateReadOnly().demoModeDesired,
            "demo mode stays off at exactly 90 seconds of inactivity");
        tickActions(1);
        expect(stateReadOnly().demoModeDesired &&
            stateReadOnly().demoStartCamera == pieces::BoardCameraView::TopDownSoccer &&
            stateReadOnly().demoCameraIndex ==
                static_cast<std::uint8_t>(pieces::BoardCameraView::TopDownSoccer),
            "demo mode enters strictly after 90 seconds and remembers original camera");
        expect(stateReadOnly().demoTicksPerMove >= 51U &&
            stateReadOnly().demoTicksPerMove <= 118U,
            "demo move duration preserves 85*(rand 600..1399)/1000 range");

        const auto duration = stateReadOnly().demoTicksPerMove;
        tickActions(duration - 1U);
        expect(stateReadOnly().demoCameraIndex ==
                (static_cast<std::uint8_t>(pieces::BoardCameraView::TopDownSoccer) + 1U) % 39U &&
            stateReadOnly().demoCycles == 1U,
            "first demo move advances sequentially and counts initial camera cycle");

        noteBoardActivity();
        tickActions(1);
        expect(!stateReadOnly().demoModeDesired,
            "new activity exits demo mode on next UDBoard tick");
        tickActions(duration);
        expect(sameCamera(stateReadOnly().worldCamera,
            boardcamera::preset(pieces::BoardCameraView::TopDownSoccer)),
            "demo exit returns to camera active when demo began");

        shutdown();
    }

    void testBoardFloatingCamera()
    {
        using namespace monopoly;
        using namespace monopoly::display;

        std::srand(7);
        expect(initialize(), "DISPLAY initializes for floating camera test");
        setBackdrop(Screen2D::Main);
        state().game3DOn = true;
        showAll2();
        state().desiredBoardCamera = pieces::BoardCameraView::FifteenTiles12;
        showAll2();

        tickActions(1);
        expect(stateReadOnly().cameraCanFloat && !stateReadOnly().floatingCameraActive,
            "starting a normal waiting camera arms exactly one legacy float");
        setTokenAnimationStackActive(true);
        tickActions(75);
        const auto anchor = boardcamera::preset(pieces::BoardCameraView::FifteenTiles12);
        expect(stateReadOnly().floatingCameraActive && !stateReadOnly().cameraCanFloat &&
            sameCamera(stateReadOnly().worldCamera, anchor),
            "normal camera completion immediately starts one floating idle at same anchor");

        const float cameraY = anchor.location[1];
        tickActions(50);
        expect(!sameCamera(stateReadOnly().worldCamera, anchor),
            "floating idle visibly traverses its 100-tick Bezier arc");
        const auto& variation = stateReadOnly().lastFloatingVariation;
        expect(std::fabs(variation[0]) <= cameraY * 0.0251F &&
            std::fabs(variation[1]) <= cameraY * 0.0251F &&
            std::fabs(variation[2]) <= cameraY * 0.0251F,
            "active TokenAnimStack uses historical 5-percent random float amplitude");
        tickActions(50);
        expect(sameCamera(stateReadOnly().worldCamera, anchor),
            "floating idle returns to the exact camera anchor after 100 ticks");
        tickActions(100);
        expect(sameCamera(stateReadOnly().worldCamera, anchor) &&
            !stateReadOnly().cameraCanFloat,
            "canFloat is consumed so no second spontaneous swoop starts");

        state().desiredBoardCamera = pieces::BoardCameraView::CornerGo;
        showAll2();
        tickActions(1);
        expect(!stateReadOnly().floatingCameraActive && stateReadOnly().cameraCanFloat,
            "new standard camera interrupts CameraIsFloatingIdle state and rearms one float");
        tickActions(75);
        expect(stateReadOnly().floatingCameraActive,
            "replacement standard camera gets its own single floating idle");

        state().desiredBoardCamera = pieces::BoardCameraView::TopDownSquare;
        showAll2();
        tickActions(1);
        tickActions(75);
        expect(!stateReadOnly().floatingCameraActive && stateReadOnly().cameraCanFloat,
            "TopDownSquare preserves canFloat flag but suppresses floating camera exactly like source");

        shutdown();
    }

}

int main()
{
    std::cout
        << "Monopoly DISPLAY contract tests\n"
        << "===============================\n";

    testEnumContract();
    testBoardCameraStateMachine();
    testBoardDemoMode();
    testBoardFloatingCamera();
    testManualMouseBoardCamera();
    testDisplayStateMachine();

    if (failures != 0)
    {
        std::cerr << failures << " DISPLAY test(s) failed.\n";
        return 1;
    }

    std::cout << "All DISPLAY contract tests passed.\n";
    return 0;
}
