#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

namespace monopoly::engine::gpuframe
{
    bool present(
        SDL_GPUDevice* device,
        SDL_Window* window
    );
}
