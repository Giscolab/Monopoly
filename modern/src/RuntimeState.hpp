#pragma once

namespace monopoly::runtime
{
    struct State
    {
        bool gamePaused = false;
        bool gameQuitRequested = false;
    };

    State& state();

    void reset();
}
