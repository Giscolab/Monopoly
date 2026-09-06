#pragma once

namespace monopoly::runtime
{
    struct State
    {
        bool gameInProgress = false;
        bool gamePaused = false;
        bool gameQuitRequested = false;
    };

    State& state();

    void reset();
}
