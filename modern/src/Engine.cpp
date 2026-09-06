#include "Engine.hpp"
#include "GPUFrame.hpp"
#include "LegacyAssets.hpp"
#include "Timers.hpp"
#include "UIMessages.hpp"
#include "ExtendedInitialization.hpp"
#include "Display.hpp"
#include "SequencePlayback.hpp"
#include "TextureCatalog.hpp"
#include "PieceMovePlayback.hpp"
#include "PieceJailPlayback.hpp"
#include "PieceIdlePlayback.hpp"
#include "PieceIdleDisplay.hpp"
#include "PieceBuildingDisplay.hpp"
#include "DiceDisplay.hpp"
#include "IBarBackdropPlayback.hpp"
#include "LocalPlayers.hpp"
#include "UserInterface.hpp"
#include "TimeStep.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <iostream>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
        std::unique_ptr<World2DRenderer> overlayRenderer;
        std::optional<data::DataId> activeBoardSequence;
        std::optional<World3DCamera> activeWorldCamera;
        pieces::PieceMovePlayback pieceMovePlayback;
        pieces::PieceJailPlayback pieceJailPlayback;
        pieces::PieceIdlePlayback pieceIdlePlayback;
        pieces::PieceIdleDisplay pieceIdleDisplay;
        pieces::PieceBuildingDisplay pieceBuildingDisplay;
        dice::Playback dicePlayback;
        dice::TwoDPlayback dice2DPlayback;
        ibar::BackdropPlayback iBarBackdropPlayback;
        bool diceQueueLockHeld{};
        std::optional<pieces::PieceIdleTransitionPlan> pendingPieceIdleTransition;
        bool pieceIdleQueueLockHeld{};
        pieces::PieceMoveSpecial activePieceMoveSpecial{pieces::PieceMoveSpecial::None};
        std::optional<pieces::PieceMoveSpecialRequest> pendingPieceMoveSpecial;
        bool pieceMoveQueueLockHeld{};
        bool victoryQueueLockReleased{};

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

        [[nodiscard]] std::expected<void, std::string> syncPieceIdlePlayback(
            SequencePlayback& session)
        {
            if (!pendingPieceIdleTransition && !pieceIdlePlayback.active())
                if (auto plan = userinterface::takePendingPieceIdleTransitionPlan())
                    pendingPieceIdleTransition = std::move(*plan);

            if (pendingPieceIdleTransition && !pieceIdlePlayback.active() &&
                !pieceMovePlayback.active() && !pieceJailPlayback.active())
            {
                pieceIdleQueueLockHeld = userinterface::gameQueueLocked();
                auto plan = std::move(*pendingPieceIdleTransition);
                pendingPieceIdleTransition.reset();
                const auto begun = pieceIdlePlayback.begin(std::move(plan));
                if (!begun)
                {
                    if (pieceIdleQueueLockHeld) userinterface::unlockGameQueue();
                    pieceIdleQueueLockHeld = false;
                    return std::unexpected(begun.error());
                }
            }

            if (!pieceIdlePlayback.active()) return {};
            const auto step = pieceIdlePlayback.tick(session);
            if (!step)
            {
                if (pieceIdleQueueLockHeld) userinterface::unlockGameQueue();
                pieceIdleQueueLockHeld = false;
                pieceIdlePlayback = {};
                return std::unexpected(step.error());
            }
            if (step->completed)
            {
                if (pieceIdleQueueLockHeld) userinterface::unlockGameQueue();
                pieceIdleQueueLockHeld = false;
            }
            return {};
        }
        [[nodiscard]] std::expected<void, std::string> syncPersistentPieceIdles(
            SequencePlayback& session, bool boardVisible)
        {
            pieces::PieceIdleDisplayContext context{};
            context.boardVisible = boardVisible;
            context.animationsEnabled = true;

            const auto& state = userinterface::ruleStateReadOnly();
            if (pieceMovePlayback.active() &&
                state.currentPlayer < state.numberOfPlayers)
                context.movingPlayer = state.currentPlayer;
            context.paddywagonPlayer = pieceJailPlayback.playerInPaddywagon();
            context.idleMovingOut = pieceIdlePlayback.movingOutPlayer();
            context.idleMovingIn = pieceIdlePlayback.movingInPlayer();

            const auto synced = pieceIdleDisplay.sync(
                state, userinterface::pieceIdleStateReadOnly(), context, session);
            if (!synced) return std::unexpected(synced.error());
            return {};
        }
        [[nodiscard]] std::expected<void, std::string> syncDicePlayback(
            SequencePlayback& session, std::uint64_t tick,
            bool boardVisible, bool iBarVisible)
        {
            if (!dicePlayback.active())
            {
                if (auto request = userinterface::takePendingDiceRoll())
                {
                    diceQueueLockHeld = true;
                    const auto begun = dicePlayback.begin(*request);
                    if (!begun)
                    {
                        userinterface::unlockGameQueue();
                        diceQueueLockHeld = false;
                        return std::unexpected(begun.error());
                    }
                }
            }

            const auto step = dicePlayback.tick(tick, boardVisible, iBarVisible,
                userinterface::ruleStateReadOnly(), session);
            if (!step)
            {
                if (diceQueueLockHeld) userinterface::unlockGameQueue();
                diceQueueLockHeld = false;
                dicePlayback.reset();
                display::cancelDiceCameraOverride();
                return std::unexpected(step.error());
            }
            if (step->cameraTakeover)
            {
                std::optional<std::uint8_t> randomFourteen;
                if (display::stateReadOnly().board3DOn)
                    randomFourteen = static_cast<std::uint8_t>(std::rand() % 14);
                display::beginDiceCameraOverride(randomFourteen);
            }
            if (step->cameraRelease)
                display::releaseDiceCameraOverride();
            if (step->queueRelease)
            {
                if (!step->cameraRelease &&
                    display::stateReadOnly().diceCameraControlActive)
                    display::endDiceCameraOverrideEarly();
                if (diceQueueLockHeld)
                {
                    userinterface::unlockGameQueue();
                    diceQueueLockHeld = false;
                }
            }
            return {};
        }
        [[nodiscard]] std::expected<void, std::string> syncPieceBuildings(
            SequencePlayback& session, bool boardVisible)
        {
            const auto synced = pieceBuildingDisplay.sync(
                userinterface::ruleStateReadOnly(), boardVisible, session);
            if (!synced) return std::unexpected(synced.error());
            return {};
        }
        [[nodiscard]] std::expected<void, std::string> syncPieceMovePlayback(
            SequencePlayback& session, bool boardVisible, std::uint64_t tick)
        {
            if (!pendingPieceMoveSpecial && !pieceJailPlayback.active())
            {
                if (auto special = userinterface::takePendingPieceMoveSpecial())
                {
                    if (special->special == pieces::PieceMoveSpecial::GoToJail)
                        pendingPieceMoveSpecial = std::move(*special);
                    // LeaveJail only clears the historical GoingToJailStatus;
                    // its projection has already moved from square 40 to 10.
                }
            }

            if (pendingPieceMoveSpecial && !pieceJailPlayback.active() &&
                !pieceMovePlayback.active())
            {
                activePieceMoveSpecial = pieces::PieceMoveSpecial::GoToJail;
                pieceMoveQueueLockHeld = userinterface::gameQueueLocked();
                const auto randomBit = pendingPieceMoveSpecial->before == 30 ?
                    static_cast<std::uint8_t>(std::rand() & 1) :
                    static_cast<std::uint8_t>(0);
                const auto begun = pieceJailPlayback.begin(*pendingPieceMoveSpecial,
                    tick, true, randomBit);
                if (!begun)
                {
                    if (pieceMoveQueueLockHeld) userinterface::unlockGameQueue();
                    pieceMoveQueueLockHeld = false;
                    pendingPieceMoveSpecial.reset();
                    activePieceMoveSpecial = pieces::PieceMoveSpecial::None;
                    return std::unexpected(begun.error());
                }
                pendingPieceMoveSpecial.reset();
            }

            if (pieceJailPlayback.active())
            {
                const auto step = pieceJailPlayback.tick(
                    tick, session, userinterface::ruleState());
                if (!step)
                {
                    if (pieceMoveQueueLockHeld) userinterface::unlockGameQueue();
                    pieceMoveQueueLockHeld = false;
                    pieceJailPlayback = {};
                    activePieceMoveSpecial = pieces::PieceMoveSpecial::None;
                    return std::unexpected(step.error());
                }
                if (step->camera)
                    display::state().desiredBoardCamera = *step->camera;
                if (step->completed)
                {
                    if (pieceMoveQueueLockHeld) userinterface::unlockGameQueue();
                    pieceMoveQueueLockHeld = false;
                    activePieceMoveSpecial = pieces::PieceMoveSpecial::None;
                }
                return {};
            }
            if (!pieceMovePlayback.active() && !pendingPieceMoveSpecial)
            {
                if (auto plan = userinterface::takePendingPieceMovePlan())
                {
                    activePieceMoveSpecial = plan->special;
                    victoryQueueLockReleased = false;
                    pieceMoveQueueLockHeld = userinterface::gameQueueLocked();
                    const auto begun = pieceMovePlayback.begin(std::move(*plan));
                    if (!begun)
                    {
                        if (pieceMoveQueueLockHeld) userinterface::unlockGameQueue();
                        pieceMoveQueueLockHeld = false;
                        activePieceMoveSpecial = pieces::PieceMoveSpecial::None;
                        return std::unexpected(begun.error());
                    }
                }
            }

            if (!pieceMovePlayback.active()) return {};

            const auto step = pieceMovePlayback.tick(boardVisible, session);
            if (!step)
            {
                if (pieceMoveQueueLockHeld) userinterface::unlockGameQueue();
                pieceMoveQueueLockHeld = false;
                return std::unexpected(step.error());
            }

            if (step->camera)
                display::state().desiredBoardCamera = *step->camera;

            if (step->looped &&
                activePieceMoveSpecial == pieces::PieceMoveSpecial::OffBoardVictory &&
                pieceMoveQueueLockHeld && !victoryQueueLockReleased)
            {
                userinterface::unlockGameQueue();
                pieceMoveQueueLockHeld = false;
                victoryQueueLockReleased = true;
            }

            if (step->completed)
            {
                if (pieceMoveQueueLockHeld) userinterface::unlockGameQueue();
                pieceMoveQueueLockHeld = false;
                victoryQueueLockReleased = false;
                activePieceMoveSpecial = pieces::PieceMoveSpecial::None;
            }
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
            const auto tick = timers::tickCount();
            if (tick > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
                return SDL_SetError("Sequence parent clock exceeds signed runtime range");
            const auto& displayState = display::stateReadOnly();
            const bool boardVisible =
                display::isBoardVisible(displayState.desired2DView);
            const bool iBarVisible =
                display::isIBarVisible(displayState.desired2DView);
            const auto& ruleState = userinterface::ruleStateReadOnly();
            const bool activePlayerCanTrade =
                ruleState.currentPlayer < ruleState.numberOfPlayers &&
                ruleState.currentPlayer < rules::MaxPlayers &&
                ruleState.players[ruleState.currentPlayer].currentSquare < 41;
            const bool tradeEligible = activePlayerCanTrade &&
                ui::localplayers::tradeSourcePlayer(
                    ruleState, ruleState.currentPlayer) != rules::MaxPlayers;
            const auto backdropSync = iBarBackdropPlayback.sync(
                ruleState, iBarVisible, ruleState.currentPlayer, *session,
                displayState.desired2DView, tradeEligible);
            if (!backdropSync)
                return SDL_SetError("IBar backdrop playback: %s",
                    backdropSync.error().c_str());
            auto& dicePrompt = userinterface::dicePromptState();
            dicePrompt.show();
            const auto dice2DSync = dice2DPlayback.sync(ruleState.dice,
                dicePrompt.currentStartTurn, iBarVisible, dicePrompt.diceRollNotification, *session);
            if (!dice2DSync)
                return SDL_SetError("Dice 2D playback: %s", dice2DSync.error().c_str());
            const auto diceSync = syncDicePlayback(*session, tick,
                boardVisible, iBarVisible);
            if (!diceSync)
                return SDL_SetError("Dice playback: %s",
                    diceSync.error().c_str());
            const auto pieceSync = syncPieceMovePlayback(*session,
                boardVisible, tick);
            if (!pieceSync)
                return SDL_SetError("Piece move playback: %s",
                    pieceSync.error().c_str());
            const auto idleSync = syncPieceIdlePlayback(*session);
            if (!idleSync)
                return SDL_SetError("Piece idle playback: %s",
                    idleSync.error().c_str());
            const auto persistentIdleSync =
                syncPersistentPieceIdles(*session, boardVisible);
            if (!persistentIdleSync)
                return SDL_SetError("Persistent piece idle: %s",
                    persistentIdleSync.error().c_str());
            const auto buildingSync = syncPieceBuildings(*session, boardVisible);
            if (!buildingSync)
                return SDL_SetError("Piece building display: %s",
                    buildingSync.error().c_str());
            const auto boardSync = syncBoardPlayback(*session, displayState);
            if (!boardSync)
                return SDL_SetError("Board sequence playback: %s",
                    boardSync.error().c_str());
            const auto updated = session->update(static_cast<std::int32_t>(tick));
            if (!updated) return SDL_SetError("Sequence playback: %s", updated.error().c_str());
            const auto viewport = display::worldViewport(displayState.viewportInUse);
            if (viewport.empty()) session->world().clearView();
            else
            {
                World3DCamera camera = displayState.worldCamera;
                if (const auto* command = session->commands().cameraState(
                        static_cast<std::uint8_t>(RenderSlot::World3D)))
                {
                    if (!activeWorldCamera) activeWorldCamera = camera;
                    if (const auto resolved = resolveWorld3DCamera(
                            *command, session->runtime()))
                        activeWorldCamera = *resolved;
                    camera = *activeWorldCamera;
                }
                else activeWorldCamera = camera;
                const auto configured = session->world().configureView(viewport, camera);
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
        if (session && session->world2D().size() && !overlayRenderer)
        {
            const auto shaderPath = std::filesystem::path(SDL_GetBasePath()) / "shaders";
            auto loaded = World2DRenderer::load(gpuDevice, shaderPath,
                SDL_GetGPUSwapchainTextureFormat(gpuDevice, gameWindow));
            if (!loaded) return SDL_SetError("World2D pipeline: %s", loaded.error().c_str());
            overlayRenderer = std::move(*loaded);
        }
        return gpuframe::present(gpuDevice, gameWindow,
            worldRenderer ? &*worldRenderer : nullptr,
            session ? &session->world() : nullptr,
            overlayRenderer.get(), session ? &session->world2D() : nullptr);
    }

    void shutdown()
    {
        pieceMovePlayback = {};
        pieceJailPlayback = {};
        pieceIdlePlayback = {};
        pieceIdleDisplay.reset();
        pieceBuildingDisplay.reset();
        if (diceQueueLockHeld) userinterface::unlockGameQueue();
        diceQueueLockHeld = false;
        dicePlayback.reset();
        dice2DPlayback.reset();
        iBarBackdropPlayback.reset();
        display::cancelDiceCameraOverride();
        pendingPieceIdleTransition.reset();
        pieceIdleQueueLockHeld = false;
        activePieceMoveSpecial = pieces::PieceMoveSpecial::None;
        pendingPieceMoveSpecial.reset();
        pieceMoveQueueLockHeld = false;
        victoryQueueLockReleased = false;
        playback.reset();
        activeBoardSequence.reset();
        activeWorldCamera.reset();
        overlayRenderer.reset();
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





