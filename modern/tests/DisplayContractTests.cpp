#include "Display.hpp"
#include "BoardCameraController.hpp"
#include "IBar.hpp"
#include "PlayerSelection.hpp"

#include <array>
#include <cstdint>
#include <cmath>
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
}

int main()
{
    std::cout
        << "Monopoly DISPLAY contract tests\n"
        << "===============================\n";

    testEnumContract();
    testBoardCameraStateMachine();
    testDisplayStateMachine();

    if (failures != 0)
    {
        std::cerr << failures << " DISPLAY test(s) failed.\n";
        return 1;
    }

    std::cout << "All DISPLAY contract tests passed.\n";
    return 0;
}
