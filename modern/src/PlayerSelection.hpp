#pragma once

#include "Actions.hpp"
#include "Display.hpp"
#include "RuleTypes.hpp"
#include "UIMessages.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace monopoly::playerselection
{
    struct PlayerInfo
    {
        std::wstring name;

        std::uint8_t token = 0;
        int aiLevel = 0;
        int citySelected = 0;

        bool customRulesDesired = false;
        bool startButtonPressed = false;
    };

    struct PlayerSlot
    {
        bool occupied = false;

        std::wstring name;

        std::uint8_t token = 0;
        std::uint8_t colour = 0;
        std::uint8_t aiLevel = 0;
    };

    struct State
    {
        PlayerInfo playerInfo;

        std::array<PlayerSlot, rules::MaxPlayers> players{};

        std::uint8_t numberOfPlayers = 0;

        bool forcedRefresh = false;
        bool firstTimeInitializationDone = false;
    };

    bool initialize();
    void shutdown();

    void switchPhase(display::PlayerSetupPhase phase);
    void update();

    void show();

    void processMessage(const actions::Message& message);

    void processLibraryMessage(
        const uimsg::Message& message
    );

    void playerButtonClicked(
        rules::PlayerNumber player
    );


    State& state();
    const State& stateReadOnly();
}





