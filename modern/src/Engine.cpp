#include "Engine.hpp"
#include "GPUFrame.hpp"
#include "LegacyAssets.hpp"
#include "Timers.hpp"
#include "UIMessages.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <iostream>
#include <cstddef>
#include <cstdint>

namespace monopoly::engine
{
    namespace
    {
        SDL_GPUDevice* gpuDevice = nullptr;
        SDL_Window* gameWindow = nullptr;
    }

    bool initialize(SDL_Window* window)
    {
        if (window == nullptr)
        {
            return false;
        }

        gameWindow = window;

        if (!uimsg::initialize())
        {
            gameWindow = nullptr;
            return false;
        }

        if (!timers::initialize())
        {
            uimsg::shutdown();
            gameWindow = nullptr;
            return false;
        }

        gpuDevice = SDL_CreateGPUDevice(
            SDL_GPU_SHADERFORMAT_DXIL |
                SDL_GPU_SHADERFORMAT_SPIRV |
                SDL_GPU_SHADERFORMAT_MSL |
                SDL_GPU_SHADERFORMAT_METALLIB,
            true,
            nullptr
        );

        if (gpuDevice == nullptr)
        {
            std::cerr
                << "SDL_CreateGPUDevice failed: "
                << SDL_GetError()
                << '\n';

            timers::shutdown();
            uimsg::shutdown();
            gameWindow = nullptr;
            return false;
        }

        if (!SDL_ClaimWindowForGPUDevice(gpuDevice, gameWindow))
        {
            std::cerr
                << "SDL_ClaimWindowForGPUDevice failed: "
                << SDL_GetError()
                << '\n';

            SDL_DestroyGPUDevice(gpuDevice);
            gpuDevice = nullptr;

            timers::shutdown();
            uimsg::shutdown();
            gameWindow = nullptr;
            return false;
        }

        // display.cpp original charge le fond 3D pendant
        // DISPLAY_initialize().
        //
        // Ce bitmap n'est pas indispensable au démarrage :
        // en cas d'absence on conserve simplement un fond noir.
        if (!legacyassets::initialize(gpuDevice))
        {
            std::cerr
                << "Legacy 3D background unavailable: "
                << SDL_GetError()
                << '\n';
        }

        return true;
    }

    bool runCyclicFunctions()
    {
        // Le vieux timer Windows tournait indépendamment à 60 Hz.
        // Notre implémentation moderne rattrape ici les ticks écoulés.
        timers::pump();

        // Ensuite viendront les équivalents de :
        // LI_SEQNCR_TimerTick()
        // LI_ANIM3D_TickScene()

        // La presentation SDL_GPU etait auparavant definie mais jamais
        // appelee. Un cycle moteur correspond maintenant a une soumission
        // de frame, comme le cycle d'affichage ArtLib original.
        return gpuframe::present(gpuDevice, gameWindow);
    }

    void shutdown()
    {
        legacyassets::shutdown();

        if (gpuDevice != nullptr)
        {
            if (gameWindow != nullptr)
            {
                SDL_ReleaseWindowFromGPUDevice(
                    gpuDevice,
                    gameWindow
                );
            }

            SDL_DestroyGPUDevice(gpuDevice);
        }

        gpuDevice = nullptr;
        gameWindow = nullptr;

        timers::shutdown();
        uimsg::shutdown();
    }
}





