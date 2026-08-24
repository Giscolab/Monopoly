#pragma once

#include <cstdint>

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

    struct State
    {
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




