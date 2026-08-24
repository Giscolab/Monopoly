#include "RuntimeState.hpp"

namespace monopoly::runtime
{
    namespace
    {
        State globalState;
    }

    State& state()
    {
        return globalState;
    }

    void reset()
    {
        // Source originale :
        //
        // GamePaused = FALSE;
        // GameQuitRequested = FALSE;

        globalState = {};
    }
}
