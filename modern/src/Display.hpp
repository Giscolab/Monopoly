#pragma once

#include <cstdint>
#include "World3DProjection.hpp"

namespace monopoly::display
{
    enum class Screen2D : std::uint8_t
    {
        // Contrat numerique de Source/monopoly/display.h.
        Black = 0,
        PlayerSelect = 1,
        PlayerSelectRules = 2,
        Options = 3,
        Portfolio = 4,
        Main = 5,
        Auction = 6,
        Trade = 7,

        // L'original utilisait -1 pour la vue courante avant le
        // premier DISPLAY_UDBOARD_Show().
        Invalid = 0xFF
    };

    // UDPSEL_SetupPhase de UDPsel.h, dans le même ordre.
    enum class Viewport3D : std::uint8_t
    {
        Main = 0,
        Status,
        Trade,
        Off
    };

    inline constexpr std::uint16_t Board3DPriority = 90; // display.h DISPLAY_Board3dPriority

    [[nodiscard]] constexpr bool isBoardVisible(Screen2D screen) noexcept
    {
        // display.h DISPLAY_IsBoardVisible uses desired2DView.
        return screen == Screen2D::Main ||
            screen == Screen2D::Trade ||
            screen == Screen2D::Portfolio;
    }

    enum class PlayerSetupPhase : std::uint8_t
    {
        None = 0,
        HiScore,
        LocalOrNetwork,
        SelectPlayer,
        EnterName,
        SelectToken,
        StartAddRemove,
        RemovePlayer,
        SelectAIStrength,
        SelectCity,
        StandardOrCustomRules,
        CustomizeRules,
        Max
    };

    // Source/monopoly/UDBoard.cpp: viewRects, CameraAngles2D[0], startup
    // interpolation endpoints and the 45-degree SetCamera3D command.
    [[nodiscard]] inline engine::World3DRect worldViewport(Viewport3D viewport) noexcept
    {
        switch (viewport)
        {
        case Viewport3D::Main: return {0, 0, 800, 450};
        case Viewport3D::Status: return {0, 0, 400, 225};
        case Viewport3D::Trade: return {200, 0, 600, 225};
        default: return {}; // Off does not publish a visible 3D viewport.
        }
    }
    [[nodiscard]] inline engine::World3DCamera initialBoardCamera() noexcept
    {
        return {{242.9F, 1200.0F, 243.0F}, {0.0045F, -0.99999F, 0.0F},
            {1.0F, 0.0F, 0.0F}, static_cast<float>(45.0 / 180.0 * 3.1415926),
            engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane};
    }

    struct State
    {
        engine::World3DCamera worldCamera = initialBoardCamera();
        Screen2D current2DView = Screen2D::Invalid;
        Screen2D desired2DView = Screen2D::PlayerSelect;

        Viewport3D viewportInUse =
            Viewport3D::Off;

        PlayerSetupPhase previousPlayerSetupPhase =
            PlayerSetupPhase::None;

        PlayerSetupPhase currentPlayerSetupPhase =
            PlayerSetupPhase::None;

        PlayerSetupPhase desiredPlayerSetupPhase =
            PlayerSetupPhase::None;

        bool showOnlyLocalPlayersOnIBar = false;
        bool showOnlyLocalAIPlayersOnIBar = false;

        bool flashCurrentToken = false;

        bool initialized = false;
    };

    bool initialize();
    void shutdown();

    void setBackdrop(Screen2D screen);

    void tickActions(std::uint64_t numberOfTicks);

    void showAll2();

    State& state();
    const State& stateReadOnly();
}




