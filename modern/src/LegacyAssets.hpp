#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

namespace monopoly::legacyassets
{
    struct Texture2D
    {
        SDL_GPUTexture* texture = nullptr;

        Uint32 width = 0;
        Uint32 height = 0;
    };

    bool initialize(SDL_GPUDevice* device);
    void shutdown();

    const Texture2D& background3D();
}
