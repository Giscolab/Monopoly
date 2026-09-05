#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include "World3DRenderer.hpp"

namespace monopoly::engine::gpuframe
{
    // The same slot recording path is used by the swapchain and GPU readback
    // integration tests. Caller retains command-buffer submission ownership.
    [[nodiscard]] std::expected<World3DRenderStats, World3DRendererError> recordWorld3D(
        SDL_GPUCommandBuffer* command, SDL_GPUTexture* target,
        Uint32 width, Uint32 height, World3DRenderer& renderer,
        const SequenceWorld3DSlot& world);
    bool present(
        SDL_GPUDevice* device,
        SDL_Window* window,
        World3DRenderer* renderer = nullptr,
        const SequenceWorld3DSlot* world = nullptr
    );
}
