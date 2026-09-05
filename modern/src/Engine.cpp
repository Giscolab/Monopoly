#include "Engine.hpp"
#include "GPUFrame.hpp"
#include "LegacyAssets.hpp"
#include "Timers.hpp"
#include "UIMessages.hpp"
#include "ExtendedInitialization.hpp"
#include "Display.hpp"
#include "SequencePlayback.hpp"
#include "TextureCatalog.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <iostream>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

namespace monopoly::engine
{
    namespace
    {
        SDL_GPUDevice* gpuDevice = nullptr;
        SDL_Window* gameWindow = nullptr;
        std::unique_ptr<SequencePlayback> playback;
        std::optional<World3DRenderer> worldRenderer;
        std::optional<data::DataId> activeBoardSequence;

        [[nodiscard]] sequence::Matrix3D boardStartupScale() noexcept
        {
            // UDBoard.cpp:998-1002 builds mxScale with SetScale(0.10f) and
            // immediately applies it through LE_SEQNCR_MoveTheWorks.
            auto scale = sequence::identity3D();
            scale.values[0] = 0.10F;
            scale.values[5] = 0.10F;
            scale.values[10] = 0.10F;
            return scale;
        }

        [[nodiscard]] std::expected<void, std::string> syncBoardPlayback(
            SequencePlayback& session, const display::State& state)
        {
            const bool shouldRun = display::isBoardVisible(state.desired2DView);
            if (!shouldRun)
            {
                if (!activeBoardSequence) return {};
                const auto stopped = session.stop(*activeBoardSequence,
                    display::Board3DPriority);
                if (!stopped) return stopped;
                activeBoardSequence.reset();
                return {};
            }

            // DISPLAY_UDBOARD_Initialize starts with city=0. Until city/render
            // options are ported, UDBoard.cpp therefore selects HMD_boardmed.
            const auto desired = data::boardMeshDataId(
                data::BoardMeshKind::ClassicMedium);
            if (activeBoardSequence == desired) return {};
            if (activeBoardSequence)
            {
                const auto stopped = session.stop(*activeBoardSequence,
                    display::Board3DPriority);
                if (!stopped) return stopped;
            }
            const auto started = session.startMoved(desired,
                display::Board3DPriority, boardStartupScale());
            if (!started) return started;
            activeBoardSequence = desired;
            return {};
        }
    }

    SequencePlayback* sequencePlayback()
    {
        if (!gpuDevice) return nullptr;
        if (!playback)
            if (auto resources = startup::resources())
                playback = std::make_unique<SequencePlayback>(std::move(resources));
        return playback.get();
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
        auto* session = sequencePlayback();
        if (session)
        {
            const auto& displayState = display::stateReadOnly();
            const auto boardSync = syncBoardPlayback(*session, displayState);
            if (!boardSync)
                return SDL_SetError("Board sequence playback: %s",
                    boardSync.error().c_str());
            const auto tick = timers::tickCount();
            if (tick > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
                return SDL_SetError("Sequence parent clock exceeds signed runtime range");
            const auto updated = session->update(static_cast<std::int32_t>(tick));
            if (!updated) return SDL_SetError("Sequence playback: %s", updated.error().c_str());
            const auto viewport = display::worldViewport(displayState.viewportInUse);
            if (viewport.empty()) session->world().clearView();
            else
            {
                const auto configured = session->world().configureView(viewport, displayState.worldCamera);
                if (!configured) return SDL_SetError("Invalid DISPLAY World3D camera/viewport");
                if (!worldRenderer)
                {
                    const auto shaderPath = std::filesystem::path(SDL_GetBasePath()) / "shaders";
                    auto loaded = World3DRenderer::load(gpuDevice, shaderPath,
                        SDL_GetGPUSwapchainTextureFormat(gpuDevice, gameWindow));
                    if (!loaded) return SDL_SetError("World3D pipeline: %s", loaded.error().detail.c_str());
                    worldRenderer = std::move(*loaded);
                }
            }
        }
        return gpuframe::present(gpuDevice, gameWindow,
            worldRenderer ? &*worldRenderer : nullptr,
            session ? &session->world() : nullptr);
    }

    void shutdown()
    {
        playback.reset();
        activeBoardSequence.reset();
        worldRenderer.reset(); // GPU objects must be released before the device.
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





