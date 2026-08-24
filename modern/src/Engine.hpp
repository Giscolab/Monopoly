#pragma once

#include <SDL3/SDL.h>

namespace monopoly::engine
{
    bool initialize(SDL_Window* window);
    bool runCyclicFunctions();
    void shutdown();
}
