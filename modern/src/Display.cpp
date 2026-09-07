#include "Display.hpp"
#include "BoardCameraController.hpp"

#include "PlayerSelection.hpp"
#include "IBar.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace monopoly::display
{
    namespace
    {
        State globalState;
        boardcamera::Controller boardCameraController;
        std::uint64_t boardCameraTick{};


        void applyDesiredBackdrop()
        {
            // =================================================
            // Partie de DISPLAY_UDBOARD_Show() qui valide
            // desired2DView -> current2DView.
            //
            // UDBOARD_SetBackdrop() ne fait PAS ce travail.
            // =================================================

            if (
                globalState.current2DView ==
                globalState.desired2DView)
            {
                return;
            }


            globalState.lastBoardActivityTick = boardCameraTick;

            switch (globalState.desired2DView)
            {
                case Screen2D::Main:
                {
                    // DISPLAY_SCREEN_MainA
                    globalState.viewportInUse =
                        Viewport3D::Main;

                    break;
                }


                case Screen2D::Portfolio:
                {
                    globalState.viewportInUse =
                        Viewport3D::Status;

                    break;
                }


                case Screen2D::Trade:
                {
                    globalState.viewportInUse =
                        Viewport3D::Trade;

                    break;
                }


                case Screen2D::PlayerSelect:
                {
                    // DISPLAY_SCREEN_Pselect
                    //
                    // Original :
                    // currentBackdropID =
                    //   DAT_LANG2/BMP_sybkgrnd
                    //
                    // Ce DAT compilé n'est pas dans l'archive,
                    // donc aucun substitut graphique n'est
                    // inventé ici.
                    globalState.viewportInUse =
                        Viewport3D::Off;

                    break;
                }


                case Screen2D::PlayerSelectRules:
                {
                    // DISPLAY_SCREEN_PselectRules
                    //
                    // Original :
                    // DAT_PAT/BMP_rnbacknd
                    globalState.viewportInUse =
                        Viewport3D::Off;

                    break;
                }


                case Screen2D::Black:
                case Screen2D::Options:
                case Screen2D::Auction:
                case Screen2D::Invalid:
                default:
                {
                    globalState.viewportInUse =
                        Viewport3D::Off;

                    break;
                }
            }


            // UDBoard.cpp original, fin de
            // DISPLAY_UDBOARD_Show():
            //
            // DISPLAY_state.current2DView =
            //     DISPLAY_state.desired2DView;

            globalState.current2DView =
                globalState.desired2DView;
        }
        void applyDesiredBoardCamera()
        {
            const bool shouldBoard3DBeOn = globalState.game3DOn &&
                isBoardVisible(globalState.desired2DView);
            const bool boardModeChanged = shouldBoard3DBeOn != globalState.board3DOn;
            const bool cameraChanged = !globalState.currentBoardCamera ||
                *globalState.currentBoardCamera != globalState.desiredBoardCamera;
            const bool revalidate = globalState.desiredCameraInvalidatedLock &&
                globalState.desiredCameraClearToValidate;
            if (cameraChanged || boardModeChanged || revalidate ||
                globalState.manualCameraRequested)
            {
                const bool forceInterrupt = !globalState.board3DOn;
                boardCameraController.requestPreset(
                    globalState.desiredBoardCamera, boardCameraTick, forceInterrupt,
                    globalState.manualCameraRequested);
                globalState.manualCameraRequested = false;
                globalState.currentBoardCamera = globalState.desiredBoardCamera;
                if (revalidate)
                {
                    globalState.desiredCameraInvalidatedLock = false;
                    globalState.desiredCameraClearToValidate = false;
                }
            }
            globalState.board3DOn = shouldBoard3DBeOn;
            globalState.worldCamera = boardCameraController.current();
        }

        [[nodiscard]] std::array<float, 3> normalized3(
            std::array<float, 3> value) noexcept
        {
            const float length = std::sqrt(value[0] * value[0] +
                value[1] * value[1] + value[2] * value[2]);
            if (length <= 0.00001F) return {};
            for (float& component : value) component /= length;
            return value;
        }

        void startFloatingIdle()
        {
            const float cameraY = boardcamera::preset(
                globalState.desiredBoardCamera).location[1];
            const float minVariation = cameraY *
                (globalState.tokenAnimationStackActive ? 0.07F : 0.08F);
            const float randomVariation = cameraY *
                (globalState.tokenAnimationStackActive ? 0.05F : 0.08F);

            if (!globalState.floatingCameraActive)
            {
                const auto& start = boardCameraController.startCamera().location;
                const auto& end = boardCameraController.endCamera().location;
                std::array<float, 3> direction{
                    start[0] - end[0], start[1] - end[1], start[2] - end[2]};
                direction = normalized3(direction);
                for (std::size_t axis = 0; axis < 3; ++axis)
                    globalState.lastFloatingVariation[axis] =
                        randomVariation * 0.33F * direction[axis];
            }

            std::array<float, 3> nextVariation{};
            do
            {
                for (float& component : nextVariation)
                    component = randomVariation *
                        (static_cast<float>(std::rand() % 100) / 100.0F);
            }
            while (nextVariation[0] + nextVariation[1] + nextVariation[2] <
                minVariation);

            for (float& component : nextVariation)
                component -= randomVariation * 0.5F;

            boardCameraController.requestFloatingIdle(boardCameraTick,
                globalState.lastFloatingVariation, nextVariation);
            globalState.lastFloatingVariation = nextVariation;
            globalState.cameraCanFloat = false;
            globalState.floatingCameraActive = true;
        }

        void updateDemoMode(std::uint64_t numberOfTicks)
        {
            if (numberOfTicks == 0) return;

            constexpr std::uint64_t DemoDelayTicks = 90U * 60U;
            constexpr float DemoBaseMoveTicks = 85.0F;

            if (globalState.lastBoardActivityTick + DemoDelayTicks <= boardCameraTick &&
                (!globalState.board3DOn || !globalState.game3DOn))
            {
                globalState.lastBoardActivityTick = boardCameraTick;
            }

            const bool shouldDemo =
                globalState.lastBoardActivityTick + DemoDelayTicks < boardCameraTick &&
                globalState.board3DOn;

            if (shouldDemo)
            {
                if (!globalState.demoModeDesired)
                {
                    globalState.cameraCanFloat = false;
                    globalState.floatingCameraActive = false;
                    globalState.demoStartCamera = globalState.desiredBoardCamera;
                    globalState.demoCameraIndex =
                        static_cast<std::uint8_t>(globalState.desiredBoardCamera);
                    globalState.demoCycles = 0;
                    globalState.demoWaitTicks = globalState.demoTicksPerMove;
                    globalState.demoTicksPerMove = static_cast<std::uint32_t>(
                        DemoBaseMoveTicks *
                        (static_cast<float>(std::rand() % 800 + 600) / 1000.0F));
                }

                globalState.demoWaitTicks += static_cast<std::uint32_t>(numberOfTicks);
                if (globalState.demoWaitTicks >= globalState.demoTicksPerMove)
                {
                    const auto startIndex =
                        static_cast<std::uint8_t>(globalState.demoStartCamera);
                    if (globalState.demoCameraIndex == startIndex)
                    {
                        ++globalState.demoCycles;
                        if (globalState.demoCycles == 2)
                            globalState.lastBoardActivityTick = boardCameraTick;
                    }

                    globalState.demoWaitTicks = 0;
                    globalState.demoCameraIndex = static_cast<std::uint8_t>(
                        (globalState.demoCameraIndex + 1U) % 39U);
                    boardCameraController.requestDemoPreset(
                        static_cast<pieces::BoardCameraView>(globalState.demoCameraIndex),
                        boardCameraTick, globalState.demoTicksPerMove);
                }
                globalState.demoModeDesired = true;
            }
            else if (globalState.demoModeDesired)
            {
                globalState.lastBoardActivityTick = boardCameraTick;
                globalState.demoModeDesired = false;
                boardCameraController.requestDemoPreset(globalState.demoStartCamera,
                    boardCameraTick, globalState.demoTicksPerMove);
            }
        }
    }

    bool initialize()
    {
        // DISPLAY_initialize() original remet d'abord son état
        // général en place puis initialise les sous-modules DISPLAY_*.

        globalState = {};
        globalState.current2DView = Screen2D::Invalid;
        globalState.desired2DView = Screen2D::PlayerSelect;

        globalState.viewportInUse =
            Viewport3D::Off;

        boardCameraTick = 0;
        boardCameraController.reset(0);
        globalState.worldCamera = boardCameraController.current();

        // DISPLAY_UDIBAR_Initialize();
        if (!ibar::initialize())
        {
            return false;
        }


        // DISPLAY_UDPSEL_Initialize();
        if (!playerselection::initialize())
        {
            ibar::shutdown();
            return false;
        }

        globalState.initialized = true;

        // Les autres modules historiques viendront dans leur ordre :
        //
        // DISPLAY_UDAUCT_Initialize
        // DISPLAY_UDBOARD_Initialize
        // DISPLAY_UDIBAR_Initialize
        // DISPLAY_UDOPTS_Initialize
        // DISPLAY_UDPIECES_Initialize
        // DISPLAY_UDPSEL_Initialize      <- présent
        // DISPLAY_UDSOUND_Initialize
        // DISPLAY_UDSTATS_Initialize
        // DISPLAY_UDTRADE_Initialize

        return true;
    }

    void shutdown()
    {
        if (globalState.initialized)
        {
            // DISPLAY_destroy() original :
            //
            // UDBOARD_SetBackdrop(DISPLAY_SCREEN_Black);
            // DISPLAY_tickActions(1);

            setBackdrop(
                Screen2D::Black
            );


            tickActions(1);
        }


        // Ordre original relatif :
        //
        // DISPLAY_UDIBAR_Destroy()
        // ...
        // DISPLAY_UDPSEL_Destroy()

        ibar::shutdown();

        playerselection::shutdown();


        boardCameraController.reset(0);
        boardCameraTick = 0;
        globalState = {};
    }

    void setBackdrop(Screen2D screen)
    {
        // ====================================================
        // UDBOARD_SetBackdrop() ORIGINAL :
        //
        // void UDBOARD_SetBackdrop(int backdrop)
        // {
        //     DISPLAY_state.desired2DView = backdrop;
        // }
        //
        // current2DView est validé plus tard par
        // DISPLAY_UDBOARD_Show().
        // ====================================================

        globalState.desired2DView =
            screen;
    }

    void beginDiceCameraOverride(
        std::optional<std::uint8_t> randomFourteen)
    {
        globalState.diceCameraControlActive = true;
        globalState.desiredCameraInvalidatedLock = true;
        globalState.desiredCameraClearToValidate = false;
        if (randomFourteen && globalState.board3DOn)
            boardCameraController.requestDiceMove(boardCameraTick, *randomFourteen);
    }

    void releaseDiceCameraOverride()
    {
        globalState.diceCameraControlActive = false;
        globalState.desiredCameraClearToValidate = true;
    }

    void endDiceCameraOverrideEarly()
    {
        // UDPieces.cpp exits the dice lock early when IBar disappears
        // without setting desiredCameraClearToValidate.
        globalState.diceCameraControlActive = false;
    }

    void cancelDiceCameraOverride()
    {
        if (globalState.desiredCameraInvalidatedLock)
            globalState.desiredCameraClearToValidate = true;
        globalState.diceCameraControlActive = false;
    }

    void noteBoardActivity() noexcept
    {
        globalState.lastBoardActivityTick = boardCameraTick;
    }

    void cycleIBarCamera(std::int32_t currentSquare, bool sequential) noexcept
    {
        static std::uint8_t topView = 0;
        using pieces::BoardCameraView;

        const auto current = globalState.desiredBoardCamera;
        const int currentIndex = static_cast<int>(current);
        BoardCameraView desired = current;

        if (sequential)
        {
            desired = static_cast<BoardCameraView>(
                (currentIndex + 1) % static_cast<int>(BoardCameraView::Count));
        }
        else
        {
            int category = 0;
            if (currentIndex >= static_cast<int>(BoardCameraView::ThreeTiles01)) ++category;
            if (currentIndex >= static_cast<int>(BoardCameraView::CornerGo)) ++category;
            if (currentIndex >= static_cast<int>(BoardCameraView::FiveTiles01)) ++category;
            if (currentIndex >= static_cast<int>(BoardCameraView::FifteenTiles01)) ++category;

            switch ((category + 1) % 5)
            {
            case 0:
                desired = static_cast<BoardCameraView>(topView++ % 3U);
                break;
            case 1:
                desired = pieces::pickCameraFor3Squares(currentSquare);
                break;
            default:
                desired = pieces::pickCameraFor15Squares(currentSquare);
                if (desired == BoardCameraView::CornerJail)
                    desired = static_cast<BoardCameraView>(topView++ % 3U);
                break;
            }
        }

        globalState.desiredBoardCamera = desired;
        if (globalState.manualMouseCamLock)
            globalState.manualCameraRequested = true;
    }

    void setTokenAnimationStackActive(bool active) noexcept
    {
        globalState.tokenAnimationStackActive = active;
    }

    void processBoardInput(const uimsg::Message& message)
    {
        if (message.type == uimsg::Type::MouseLeftDown ||
            message.type == uimsg::Type::KeyboardPressed)
        {
            noteBoardActivity();
        }
        switch (message.type)
        {
        case uimsg::Type::MouseLeftDown:
            globalState.mouseLeftPressed = true;
            return;
        case uimsg::Type::MouseLeftUp:
            globalState.mouseLeftPressed = false;
            return;
        case uimsg::Type::MouseRightDown:
            globalState.mouseRightPressed = true;
            return;
        case uimsg::Type::MouseRightUp:
            globalState.mouseRightPressed = false;
            return;
        default:
            break;
        }

        if (message.type != uimsg::Type::MouseMoved ||
            !globalState.mouseLeftPressed || !globalState.board3DOn ||
            globalState.viewportInUse == Viewport3D::Off)
            return;

        const auto rect = worldViewport(globalState.viewportInUse);
        if (message.numberA < rect.left || message.numberA > rect.right ||
            message.numberB < rect.top || message.numberB > rect.bottom)
            return;

        const auto clampDelta = [](std::int64_t value) noexcept {
            return static_cast<std::int32_t>(std::clamp<std::int64_t>(value,
                std::numeric_limits<std::int32_t>::min(),
                std::numeric_limits<std::int32_t>::max()));
        };
        const bool verticalOrbit = globalState.mouseRightPressed ||
            (message.numberE & uimsg::MouseModifierControl) != 0;
        if (boardCameraController.requestManualMouseMove(
                clampDelta(message.numberC), clampDelta(message.numberD),
                verticalOrbit, boardCameraTick))
        {
            globalState.manualMouseCamLock = true;
            globalState.manualMouseCamTime = boardCameraTick;
            globalState.cameraCanFloat = false;
            globalState.floatingCameraActive = false;
            globalState.lastBoardActivityTick = boardCameraTick;
            globalState.worldCamera = boardCameraController.current();
        }
    }

    void showAll2()
    {
        // ====================================================
        // DISPLAY_showAll2() original.
        //
        // Modules actuellement portés :
        //
        //   DISPLAY_UDBOARD_Show
        //   DISPLAY_UDIBAR_Show
        //   DISPLAY_UDPSEL_Show
        //
        // On conserve leur ORDRE original.
        // ====================================================


        // DISPLAY_UDBOARD_Show().
        applyDesiredBackdrop();
        applyDesiredBoardCamera();


        // DISPLAY_UDIBAR_Show().
        ibar::show();


        // DISPLAY_UDPSEL_Show().
        playerselection::show();
    }

    void tickActions(std::uint64_t numberOfTicks)
    {
        // ====================================================
        // DISPLAY_tickActions() original :
        //
        // DISPLAY_UDBOARD_TickActions(numberOfTicks);
        // DISPLAY_UDIBAR_TickActions(numberOfTicks);
        // DISPLAY_UDPIECES_TickActions(numberOfTicks);
        // DISPLAY_UDSOUND_tickActions(numberOfTicks);
        //
        // DISPLAY_showAll2();
        // ====================================================


        // DISPLAY_UDBOARD_TickActions :
        // aucune logique temporelle du Board n'est encore
        // nécessaire dans cette tranche.


        boardCameraTick += numberOfTicks;
        updateDemoMode(numberOfTicks);
        const auto cameraUpdate = boardCameraController.tick(boardCameraTick);
        globalState.worldCamera = cameraUpdate.camera;
        if (globalState.manualMouseCamLock &&
            !boardCameraController.manualMouseActive())
            globalState.manualMouseCamLock = false;
        if (cameraUpdate.startedWaitingMove)
        {
            globalState.cameraCanFloat = true;
            globalState.floatingCameraActive = false;
        }
        if (globalState.cameraCanFloat && !boardCameraController.moving() &&
            !globalState.demoModeDesired && globalState.board3DOn &&
            globalState.game3DOn &&
            globalState.desiredBoardCamera != pieces::BoardCameraView::TopDownSquare &&
            !globalState.manualMouseCamLock)
        {
            startFloatingIdle();
        }
        if (globalState.manualMouseCamLock &&
            boardCameraTick > globalState.manualMouseCamTime +
                60U * 20U)
        {
            globalState.manualMouseCamLock = false;
            boardCameraController.releaseManualMouse();
            globalState.desiredCameraInvalidatedLock = true;
            globalState.desiredCameraClearToValidate = true;
        }

        // DISPLAY_UDIBAR_TickActions().
        ibar::tickActions(
            numberOfTicks
        );


        // UDPIECES / UDSOUND seront insérés ici dans leur
        // emplacement historique lorsqu'ils seront portés.


        // IMPORTANT :
        // DISPLAY_showAll2() vient APRES tous les TickActions.
        showAll2();
    }

    State& state()
    {
        return globalState;
    }

    const State& stateReadOnly()
    {
        return globalState;
    }
}





