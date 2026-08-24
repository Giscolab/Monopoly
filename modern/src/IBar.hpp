#pragma once

#include "IBarLayout.hpp"
#include "RuleTypes.hpp"
#include "UIMessages.hpp"

#include <array>
#include <cstdint>

namespace monopoly::ibar
{
    struct PlayerDisplay
    {
        bool visible = false;

        bool local = false;

        bool localHuman = false;

        bool localAI = false;


        layout::Rect rect{};
    };


    struct State
    {
        std::array<
            PlayerDisplay,
            rules::MaxPlayers
        > players{};


        int playerLastMouseOver = -1;

        int playerCurrentMouseOver = -1;


        bool initialized = false;
    };


    bool initialize();


    void shutdown();


    void tickActions(
        std::uint64_t numberOfTicks
    );


    void show();


    void processLibraryMessage(
        const uimsg::Message& message
    );


    State& state();


    const State& stateReadOnly();
}
