#include "Engine.hpp"
#include "AudioRuntime.hpp"
#include "UDSoundRuntime.hpp"
#include "UDPennyVoice.hpp"
#include "UISound.hpp"
#include "AIUtility.hpp"
#include "GPUFrame.hpp"
#include "LegacyAssets.hpp"
#include "Timers.hpp"
#include "UIMessages.hpp"
#include "ExtendedInitialization.hpp"
#include "Display.hpp"
#include "BoardRules.hpp"
#include "BoardOwnershipHighlight.hpp"
#include "BoardBackdropPlayback.hpp"
#include "BoardLightingController.hpp"
#include "RuleBuildings.hpp"
#include "RuntimeState.hpp"
#include "SequencePlayback.hpp"
#include "TextureCatalog.hpp"
#include "PieceMovePlayback.hpp"
#include "PieceRuntime.hpp"
#include "PieceJailPlayback.hpp"
#include "PieceIdlePlayback.hpp"
#include "PieceIdleDisplay.hpp"
#include "PieceBuildingDisplay.hpp"
#include "PieceShadowDisplay.hpp"
#include "AuctionPlayback.hpp"
#include "AuctionPennyBagsPlayback.hpp"
#include "TradeBackdropPlayback.hpp"
#include "TradeTokenPlayback.hpp"
#include "TradeActionButtonPlayback.hpp"
#include "OptionsFilePlayback.hpp"
#include "OptionsNavigationPlayback.hpp"
#include "StatsPlayback.hpp"
#include "ChatRecipientPlayback.hpp"
#include "ChatOptionPlayback.hpp"
#include "ChatFluffPlayback.hpp"
#include "StatsBankPlayback.hpp"
#include "StatsCalculatorPlayback.hpp"
#include "StatsCalculatorDeedPickerPlayback.hpp"
#include "StatsPlayerPlayback.hpp"
#include "StatsPlayerCashPlayback.hpp"
#include "StatsPlayerAuxPlayback.hpp"
#include "StatsFutureImmunityPlayback.hpp"
#include "StatsDeedPlayback.hpp"
#include "StatsDeedFloaterPlayback.hpp"
#include "StatsDeedBarPlayback.hpp"
#include "OptionsOptionPlayback.hpp"
#include "OptionsTogglePlayback.hpp"
#include "OptionsHelpPlayback.hpp"
#include "TradePropertyPlayback.hpp"
#include "TradeOfferIconPlayback.hpp"
#include "TradeCashDialogPlayback.hpp"
#include "TradeContractDialogPlayback.hpp"
#include "DiceDisplay.hpp"
#include "IBar.hpp"
#include "IBarBackdropPlayback.hpp"
#include "LocalPlayers.hpp"
#include "UserInterface.hpp"
#include "TimeStep.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <iostream>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace monopoly::engine
{
    void disableAudioPlayback(std::string_view context,
        const std::string& error) noexcept;

    namespace
    {
        SDL_GPUDevice* gpuDevice = nullptr;
        SDL_Window* gameWindow = nullptr;
        std::unique_ptr<SequencePlayback> playback;
        std::unique_ptr<audio::Runtime> audioRuntime;
        udsound::Runtime monopolySoundRuntime;
        std::vector<sequence::SequenceNodeId> activeSequenceSounds;
        bool audioDisabled{};
        bool tokenVoiceQueueLockHeld{};
        std::optional<std::uint8_t> activePieceMoveToken;
        std::optional<World3DRenderer> worldRenderer;
        std::unique_ptr<World2DRenderer> overlayRenderer;
        std::optional<data::DataId> activeBoardSequence;
        std::optional<World3DCamera> activeWorldCamera;
        pieces::PieceMovePlayback pieceMovePlayback;
        pieces::PieceJailPlayback pieceJailPlayback;
        pieces::PieceIdlePlayback pieceIdlePlayback;
        pieces::PieceIdleDisplay pieceIdleDisplay;
        pieces::PieceBuildingDisplay pieceBuildingDisplay;
        pieces::PieceShadowDisplay pieceShadowDisplay;
        auctionui::Playback auctionPlayback;
        auctionui::PennyBagsPlayback auctionPennyBagsPlayback;
        tradeui::BackdropPlayback tradeBackdropPlayback;
        tradeui::TokenPlayback tradeTokenPlayback;
        tradeui::ActionButtonPlayback tradeActionButtonPlayback;
        optionsui::FilePlayback optionsFilePlayback;
        optionsui::NavigationPlayback optionsNavigationPlayback;
        statsui::Playback statsPlayback;
        chat::RecipientPlayback chatRecipientPlayback;
        chat::OptionPlayback chatOptionPlayback;
        chat::FluffPlayback chatFluffPlayback;
        statsui::BankPlayback statsBankPlayback;
        statsui::CalculatorPlayback statsCalculatorPlayback;
        statsui::CalculatorDeedPickerPlayback statsCalculatorDeedPickerPlayback;
        statsui::PlayerPlayback statsPlayerPlayback;
        statsui::PlayerCashPlayback statsPlayerCashPlayback;
        statsui::PlayerAuxPlayback statsPlayerAuxPlayback;
        statsui::FutureImmunityPlayback statsFutureImmunityPlayback;
        statsui::DeedPlayback statsDeedPlayback;
        statsui::DeedFloaterPlayback statsDeedFloaterPlayback;
        statsui::DeedBarPlayback statsDeedBarPlayback;
        optionsui::OptionPlayback optionsOptionPlayback;
        optionsui::TogglePlayback optionsTogglePlayback;
        optionsui::HelpPlayback optionsHelpPlayback;
        tradeui::PropertyPlayback tradePropertyPlayback;
        tradeui::OfferIconPlayback tradeOfferIconPlayback;
        tradeui::CashDialogPlayback tradeCashDialogPlayback;
        tradeui::ContractDialogPlayback tradeContractDialogPlayback;
        boarddisplay::BoardBackdropPlayback boardBackdropPlayback;
        boarddisplay::OwnershipHighlightPlayback ownershipHighlightPlayback;
        boarddisplay::BoardLightingController boardLightingController;
        pieces::TokenPoseTracker lightingTokenPoseTracker;
        std::uint64_t lastBoardLightingTick{};
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

        struct IBarBSSMAvailability
        {
            ibar::layout::PropertyMask buildProperties{};
            ibar::layout::PropertyMask sellProperties{};
            ibar::layout::PropertyMask mortgageProperties{};
            ibar::layout::PropertyMask unmortgageProperties{};
        };

        [[nodiscard]] IBarBSSMAvailability iBarBSSMAvailability(
            const rules::GameState& state,
            rules::PlayerNumber player)
        {
            IBarBSSMAvailability result{};
            if (player >= state.numberOfPlayers || player >= rules::MaxPlayers)
                return result;

            for (std::uint8_t squareNo = 0; squareNo < rules::SquareCount; ++squareNo)
            {
                const auto bit = ibar::layout::propertyBit(squareNo);
                if (bit == 0) continue;

                if (rules::buildings::testBuildingPlacement(
                        state, player, squareNo, true).error == 0)
                    result.buildProperties |= bit;
                if (rules::buildings::testBuildingPlacement(
                        state, player, squareNo, false).error == 0)
                    result.sellProperties |= bit;

                const auto& square = state.squares[squareNo];
                if (square.owner != player) continue;
                const auto squareType =
                    static_cast<rules::board::SquareType>(squareNo);
                const auto& definition = rules::board::definition(squareType);

                if (!square.mortgaged)
                {
                    bool groupHasBuildings = false;
                    for (std::uint8_t testNo = 0; testNo < rules::SquareCount; ++testNo)
                    {
                        const auto testType =
                            static_cast<rules::board::SquareType>(testNo);
                        if (rules::board::definition(testType).group == definition.group &&
                            state.squares[testNo].owner == player &&
                            state.squares[testNo].houses != 0)
                        {
                            groupHasBuildings = true;
                            break;
                        }
                    }
                    if (!groupHasBuildings)
                        result.mortgageProperties |= bit;
                }
                else
                {
                    const std::int64_t mortgage = definition.mortgageCost;
                    const std::int64_t fees = mortgage +
                        (mortgage * state.options.interestRate + 50) / 100;
                    if (state.players[player].cash >= fees)
                        result.unmortgageProperties |= bit;
                }
            }
            return result;
        }

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
            const bool shouldRun = state.board3DOn;
            if (!shouldRun)
            {
                if (!activeBoardSequence) return {};
                const auto stopped = session.stop(*activeBoardSequence,
                    display::Board3DPriority);
                if (!stopped) return stopped;
                activeBoardSequence.reset();
                return {};
            }

            // UDBoard.cpp:992-995 selects the medium classic mesh for city 0
            // and the medium city mesh for every non-zero USA city.
            const auto desired = data::boardMeshDataId(
                data::usaBoardMeshForCity(state.city));
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
            context.animationsEnabled = display::stateReadOnly().optionTokenAnimationsOn;

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

        [[nodiscard]] std::expected<void, std::string> syncPieceShadows(
            SequencePlayback& session, const rules::GameState& ruleState,
            const display::State& displayState, bool boardVisible)
        {
            pieces::PieceShadowDisplayContext context{};
            context.boardVisible = boardVisible;
            context.lightingOn = displayState.optionLightingOn;
            context.game3DOn = displayState.game3DOn;
            context.paddywagonPlayer = pieceJailPlayback.playerInPaddywagon();
            context.goingToJailStatus = pieceJailPlayback.state();
            context.currentUIPlayer = ui::localplayers::currentUIPlayer();

            for (rules::PlayerNumber player = 0;
                 player < rules::MaxPlayers; ++player)
            {
                const auto idle = pieceIdleDisplay.shownSequence(player);
                if (idle != data::EmptyDataId)
                    context.runtimeSources.idleSequences[player] = idle;
            }

            const auto moving = pieceMovePlayback.currentSequence();
            if (moving != data::EmptyDataId)
            {
                context.runtimeSources.movingSequence = moving;
                context.currentPlayerTokenSequenceActive = true;
            }

            context.runtimeSources.playerMovingOut = pieceIdlePlayback.movingOutPlayer();
            const auto movingOut = pieceIdlePlayback.movingOutSequence();
            if (movingOut != data::EmptyDataId)
                context.runtimeSources.playerMovingOutSequence = movingOut;

            context.runtimeSources.playerMovingIn = pieceIdlePlayback.movingInPlayer();
            const auto movingIn = pieceIdlePlayback.movingInSequence();
            if (movingIn != data::EmptyDataId)
                context.runtimeSources.playerMovingInSequence = movingIn;

            const auto jailToken = pieceJailPlayback.activeTokenSequence();
            if (jailToken != data::EmptyDataId)
                context.runtimeSources.jailTokenAnimation = jailToken;

            const auto synced = pieceShadowDisplay.sync(
                ruleState, context, session);
            if (!synced) return std::unexpected(synced.error());
            return {};
        }

        [[nodiscard]] std::expected<void, std::string> syncBoardLighting(
            SequencePlayback& session, const rules::GameState& ruleState,
            const display::State& displayState)
        {
            const auto tick = displayState.boardTick;
            const auto elapsed = tick >= lastBoardLightingTick
                ? tick - lastBoardLightingTick
                : tick;
            lastBoardLightingTick = tick;
            if (elapsed == 0)
            {
                if (worldRenderer)
                    worldRenderer->setLighting(boardLightingController.current());
                return {};
            }

            boarddisplay::BoardLightingInputs inputs{};
            inputs.game3DOn = displayState.game3DOn;
            inputs.board3DOn = displayState.board3DOn;
            inputs.lightingOn = displayState.optionLightingOn;
            inputs.tokenAnimationActive = pieceMovePlayback.active();
            inputs.tick = tick;
            inputs.numberOfTicks = elapsed;
            inputs.lastBoardActivityTick = displayState.lastBoardActivityTick;

            const auto player = ruleState.currentPlayer;
            if (player < ruleState.numberOfPlayers && player < rules::MaxPlayers)
            {
                const auto& source = ruleState.players[player];
                if (source.colour >= rules::MaxPlayerColours)
                    return std::unexpected(
                        "board spotlight player colour is outside legacy 0..5 range");
                inputs.playerColour = source.colour;

                auto pose = pieces::tokenOrientation(source.currentSquare);
                if (!pose)
                    return std::unexpected(
                        "board spotlight current player square is outside legacy range");

                if (pieceMovePlayback.active())
                {
                    pieces::TokenRuntimeSources sources{};
                    const auto idle = pieceIdleDisplay.shownSequence(player);
                    if (idle != data::EmptyDataId)
                        sources.idleSequences[player] = idle;
                    const auto moving = pieceMovePlayback.currentSequence();
                    if (moving != data::EmptyDataId)
                    {
                        sources.movingSequence = moving;
                        *pose = lightingTokenPoseTracker.locate(
                            player, ruleState.numberOfPlayers, sources,
                            session.runtime());
                    }
                }
                inputs.tokenPosition = {pose->x, pose->y, pose->z};
            }
            else if (displayState.board3DOn)
                return std::unexpected(
                    "board spotlight current player is outside active player range");

            const auto lighting = boardLightingController.tick(inputs);
            if (!lighting) return std::unexpected(lighting.error());
            if (worldRenderer) worldRenderer->setLighting(*lighting);
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
            if (step->announceRoll)
            {
                const auto& projected = userinterface::ruleStateReadOnly();
                const auto total = static_cast<std::uint8_t>(
                    projected.dice[0] + projected.dice[1]);
                if (const auto reaction = penny::diceRollPennybagsReaction(
                        total, display::stateReadOnly().optionTokenAnimationsOn))
                    engine::playPennybagsVoice(reaction->voice,
                        reaction->policy, reaction->watchAfterStart);
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
        void reconcileTokenVoiceQueueLock(bool shouldHold)
        {
            if (shouldHold && !tokenVoiceQueueLockHeld)
            {
                userinterface::lockGameQueue();
                tokenVoiceQueueLockHeld = true;
            }
            else if (!shouldHold && tokenVoiceQueueLockHeld)
            {
                userinterface::unlockGameQueue();
                tokenVoiceQueueLockHeld = false;
            }
        }

        [[nodiscard]] std::expected<void, std::string> playPieceTokenVoice(
            std::uint8_t token, udsound::TokenVoiceLine line,
            udsound::TokenVoiceClipPolicy policy, bool watchAfterStart)
        {
            auto* output = audioPlayback();
            if (output == nullptr) return {};
            auto played = monopolySoundRuntime.tokenVoice(*output,
                display::stateReadOnly().optionTokenVoicesOn, token, line, policy,
                static_cast<std::uint32_t>(std::rand()));
            if (!played)
            {
                disableAudioPlayback("Token voice disabled audio", played.error());
                return {};
            }
            if (watchAfterStart && played->started)
                monopolySoundRuntime.watchTokenVoice(*output, token);
            auto watched = monopolySoundRuntime.syncTokenVoices(*output);
            if (!watched)
            {
                disableAudioPlayback("Token voice watch disabled audio", watched.error());
                return {};
            }
            reconcileTokenVoiceQueueLock(*watched);
            return {};
        }

        [[nodiscard]] std::expected<void, std::string> playPennybagsComment(
            udsound::PennybagsVoice line,
            udsound::TokenVoiceClipPolicy policy, bool watchAfterStart)
        {
            auto* output = audioPlayback();
            if (output == nullptr) return {};
            auto played = monopolySoundRuntime.pennybagsVoice(*output,
                display::stateReadOnly().optionHostCommentsOn, line, policy,
                static_cast<std::uint32_t>(std::rand()));
            if (!played)
            {
                disableAudioPlayback("Pennybags voice disabled audio", played.error());
                return {};
            }
            if (watchAfterStart && played->started)
                monopolySoundRuntime.watchPennybags(*output);
            auto watched = monopolySoundRuntime.syncTokenVoices(*output);
            if (!watched)
            {
                disableAudioPlayback("Pennybags voice watch disabled audio", watched.error());
                return {};
            }
            reconcileTokenVoiceQueueLock(*watched);
            return {};
        }

        [[nodiscard]] std::expected<void, std::string> playPennybagsSpecific(
            data::DataId wave, udsound::TokenVoiceClipPolicy policy,
            bool watchAfterStart)
        {
            auto* output = audioPlayback();
            if (output == nullptr) return {};
            auto played = monopolySoundRuntime.pennybagsSpecific(*output,
                display::stateReadOnly().optionHostCommentsOn, wave, policy);
            if (!played)
            {
                disableAudioPlayback("Pennybags specific disabled audio", played.error());
                return {};
            }
            if (watchAfterStart && played->started)
                monopolySoundRuntime.watchPennybags(*output);
            auto watched = monopolySoundRuntime.syncTokenVoices(*output);
            if (!watched)
            {
                disableAudioPlayback("Pennybags specific watch disabled audio", watched.error());
                return {};
            }
            reconcileTokenVoiceQueueLock(*watched);
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
                if (step->playSiren && display::stateReadOnly().optionTokenVoicesOn)
                {
                    if (auto* output = audioPlayback())
                    {
                        const auto siren = monopolySoundRuntime.siren(
                            *output, static_cast<std::uint8_t>(std::rand() % udsound::SirenCount));
                        if (!siren)
                        {
                            disableAudioPlayback("Paddywagon siren disabled audio", siren.error());
                            return {};
                        }
                    }
                }
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
                    activePieceMoveToken = plan->token;
                    victoryQueueLockReleased = false;
                    pieceMoveQueueLockHeld = userinterface::gameQueueLocked();
                    if (plan->special == pieces::PieceMoveSpecial::OffBoardBankrupt ||
                        plan->special == pieces::PieceMoveSpecial::OffBoardVictory)
                    {
                        bool localHuman = false;
                        const auto& projected = userinterface::ruleStateReadOnly();
                        for (rules::PlayerNumber player = 0;
                             player < projected.numberOfPlayers && player < rules::MaxPlayers; ++player)
                            if (projected.players[player].token == plan->token)
                            {
                                localHuman = ui::localplayers::slotIsLocalHumanPlayer(player);
                                break;
                            }
                        const bool victory = plan->special == pieces::PieceMoveSpecial::OffBoardVictory;
                        if (const auto host = penny::offBoardPennybagsReaction(victory, localHuman))
                        {
                            const auto voice = playPennybagsComment(
                                host->voice, host->policy, host->watchAfterStart);
                            if (!voice) return voice;
                        }
                        if (!victory && !localHuman)
                        {
                            const auto line = (std::rand() & 1) == 0 ?
                                udsound::TokenVoiceLine::GiveUp : udsound::TokenVoiceLine::Bankrupt;
                            const auto voice = playPieceTokenVoice(plan->token, line,
                                udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay, false);
                            if (!voice) return voice;
                        }
                    }
                    const auto begun = pieceMovePlayback.begin(std::move(*plan));
                    if (!begun)
                    {
                        if (pieceMoveQueueLockHeld) userinterface::unlockGameQueue();
                        pieceMoveQueueLockHeld = false;
                        activePieceMoveSpecial = pieces::PieceMoveSpecial::None;
                        activePieceMoveToken.reset();
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

            if (step->passedGo)
            {
                const auto voice = playPennybagsComment(
                    udsound::PennybagsVoice::CollectMoney_Go,
                    udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay, false);
                if (!voice) return voice;
            }

            if (step->looped &&
                activePieceMoveSpecial == pieces::PieceMoveSpecial::OffBoardVictory &&
                pieceMoveQueueLockHeld && !victoryQueueLockReleased)
            {
                userinterface::unlockGameQueue();
                pieceMoveQueueLockHeld = false;
                victoryQueueLockReleased = true;
                if (activePieceMoveToken)
                {
                    const auto voice = playPieceTokenVoice(*activePieceMoveToken,
                        udsound::TokenVoiceLine::WonGame,
                        udsound::TokenVoiceClipPolicy::SkipIfOldSoundPlaying, true);
                    if (!voice) return voice;
                }
            }

            if (step->completed)
            {
                if (activePieceMoveToken)
                {
                    const auto& projected = userinterface::ruleStateReadOnly();
                    for (rules::PlayerNumber player = 0;
                         player < projected.numberOfPlayers && player < rules::MaxPlayers; ++player)
                    {
                        if (projected.players[player].token != *activePieceMoveToken)
                            continue;
                        const auto square = projected.players[player].currentSquare;
                        penny::LandingEconomics economics{};
                        const bool cardMove = display::stateReadOnly().justReadACardHack;
                        if (square == 4)
                        {
                            economics.totalWorth = ai::totalWorth(projected, player);
                            economics.valid = true;
                        }
                        else if (square < 40 && projected.squares[square].owner != rules::NobodyPlayer &&
                                 projected.squares[square].owner != player && !cardMove)
                        {
                            economics.totalWorth = ai::totalWorth(projected, player);
                            economics.rent = ai::rentIfSteppedOn(projected,
                                static_cast<rules::board::SquareType>(square),
                                ai::propertiesOwnedByPlayer(projected, player));
                            economics.valid = true;
                        }
                        if (auto* output = audioPlayback())
                        {
                            const auto announcement = penny::squareAnnouncementWave(
                                output->boardEdition(), display::stateReadOnly().city, square);
                            if (announcement)
                            {
                                const auto spoken = playPennybagsSpecific(*announcement,
                                    udsound::TokenVoiceClipPolicy::SkipIfOldSoundPlaying, true);
                                if (!spoken) return spoken;
                            }
                        }
                        const auto reactionRandom = static_cast<std::uint32_t>(std::rand() % 100);
                        const auto pennybagsReaction = penny::landedOnSquarePennybagsReaction(
                            projected, player, square, reactionRandom);
                        if (pennybagsReaction)
                        {
                            const auto voice = playPennybagsComment(pennybagsReaction->voice,
                                pennybagsReaction->policy, pennybagsReaction->watchAfterStart);
                            if (!voice) return voice;
                        }
                        const auto reaction = penny::landedOnSquareReaction(projected, player, square,
                            reactionRandom, economics, cardMove);
                        if (reaction)
                        {
                            const auto voice = playPieceTokenVoice(*activePieceMoveToken, reaction->line,
                                reaction->policy, reaction->watchAfterStart);
                            if (!voice) return voice;
                        }
                        display::state().justReadACardHack = false;
                        break;
                    }
                }
                if (pieceMoveQueueLockHeld) userinterface::unlockGameQueue();
                pieceMoveQueueLockHeld = false;
                victoryQueueLockReleased = false;
                activePieceMoveSpecial = pieces::PieceMoveSpecial::None;
                activePieceMoveToken.reset();
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


    audio::Runtime* audioPlayback()
    {
        if (audioDisabled)
            return nullptr;
        if (!audioRuntime)
            if (auto resources = startup::resources())
                audioRuntime = std::make_unique<audio::Runtime>(std::move(resources));
        return audioRuntime.get();
    }

    udsound::Runtime* monopolySoundPlayback()
    {
        return &monopolySoundRuntime;
    }

    void disableAudioPlayback(std::string_view context, const std::string& error) noexcept
    {
        std::cerr << context << ": " << error << '\r\n';
        monopolySoundRuntime.reset(audioRuntime.get());
        if (audioRuntime) audioRuntime->stopAll();
        activeSequenceSounds.clear();
        if (tokenVoiceQueueLockHeld) userinterface::unlockGameQueue();
        tokenVoiceQueueLockHeld = false;
        audioDisabled = true;
    }

    void playWarningSound() noexcept
    {
        if (auto* output = audioPlayback())
        {
            const auto result = monopolySoundRuntime.warning(*output);
            if (!result) disableAudioPlayback("Warning sound disabled audio", result.error());
        }
    }

    void playClickSound() noexcept
    {
        if (auto* output = audioPlayback())
        {
            const auto result = monopolySoundRuntime.click(*output);
            if (!result) disableAudioPlayback("Click sound disabled audio", result.error());
        }
    }

    void playBuildSound() noexcept
    {
        if (auto* output = audioPlayback())
        {
            const auto result = monopolySoundRuntime.build(*output);
            if (!result) disableAudioPlayback("Build sound disabled audio", result.error());
        }
    }

    void playUnbuildSound() noexcept
    {
        if (auto* output = audioPlayback())
        {
            const auto result = monopolySoundRuntime.unbuild(*output);
            if (!result) disableAudioPlayback("Unbuild sound disabled audio", result.error());
        }
    }

    void playSaveFailureSound() noexcept
    {
        if (auto* output = audioPlayback())
        {
            const auto result = monopolySoundRuntime.saveFailure(*output);
            if (!result) disableAudioPlayback("Save failure sound disabled audio", result.error());
        }
    }

    void playTokenVoice(std::uint8_t token, udsound::TokenVoiceLine line,
        udsound::TokenVoiceClipPolicy policy, bool watchAfterStart) noexcept
    {
        (void)playPieceTokenVoice(token, line, policy, watchAfterStart);
    }

    void playPennybagsVoice(udsound::PennybagsVoice line,
        udsound::TokenVoiceClipPolicy policy, bool watchAfterStart) noexcept
    {
        (void)playPennybagsComment(line, policy, watchAfterStart);
    }

    bool spokenPostLockSlotEmpty() noexcept
    {
        return monopolySoundRuntime.postLockVoiceSlotEmpty();
    }

    bool isUsaBoardEdition() noexcept
    {
        const auto resources = startup::resources();
        return !resources || resources->context().board == data::BoardEdition::Usa;
    }

    void playJailChoiceHostComment() noexcept
    {
        auto* output = audioPlayback();
        if (output == nullptr) return;
        if (output->boardEdition() == data::BoardEdition::Europe)
        {
            // !USA_VERSION calls WAV_pb235 specifically: alternate 2 of
            // PB_JailPayMoneyOrTryForDoubles (WAV_pb233..WAV_pb235).
            const auto pb235 = udsound::pennybagsVoiceAt(output->boardEdition(),
                udsound::PennybagsVoice::JailPayMoneyOrTryForDoubles, 2);
            if (pb235)
                (void)playPennybagsSpecific(*pb235,
                    udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay, false);
        }
        else
            (void)playPennybagsComment(udsound::PennybagsVoice::JailPayMoneyOrTryForDoubles,
                udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay, false);
    }

    std::expected<void, std::string> syncSequenceAudio(SequencePlayback& session)
    {
        auto* output = audioPlayback();
        if (output == nullptr)
            return {};

        const auto instances = session.runtime().soundInstances();
        std::vector<sequence::SequenceNodeId> next;
        next.reserve(instances.size());
        for (const auto& instance : instances)
        {
            const audio::PlaybackKey key{audio::PlaybackDomain::Sequence, instance.node};
            const bool known = std::find(activeSequenceSounds.begin(),
                activeSequenceSounds.end(), instance.node) != activeSequenceSounds.end();
            if (!known)
            {
                const auto started = output->play(key, instance.contentsDataId,
                    1.0F, instance.endingAction == 3);
                if (!started)
                    return std::unexpected(started.error());
            }
            else
                output->setLooping(key, instance.endingAction == 3);
            next.push_back(instance.node);
        }

        for (const auto node : activeSequenceSounds)
        {
            if (std::find(next.begin(), next.end(), node) == next.end())
                output->stop({audio::PlaybackDomain::Sequence, node});
        }
        activeSequenceSounds = std::move(next);
        output->update();
        return {};
    }

    std::expected<void, std::string> syncMonopolyAudio(
        const rules::GameState& ruleState,
        const display::State& displayState)
    {
        auto* output = audioPlayback();
        if (output == nullptr) return {};
        const auto count = std::min<rules::PlayerNumber>(
            ruleState.numberOfPlayers, rules::MaxPlayers);
        for (rules::PlayerNumber player = 0; player < count; ++player)
        {
            const auto cash = monopolySoundRuntime.syncCash(
                *output, iBarBackdropPlayback.scoreTextState(player).lastCashChange);
            if (!cash) return cash;
        }
        const auto music = monopolySoundRuntime.syncMusic(
            *output, runtime::state().gameInProgress,
            displayState.optionMusicOn, displayState.optionMusicTuneIndex);
        if (!music) return music;
        const auto watched = monopolySoundRuntime.syncTokenVoices(*output);
        if (!watched) return std::unexpected(watched.error());
        reconcileTokenVoiceQueueLock(*watched);
        return {};
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
                << '\r\n';

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
                << '\r\n';

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
                << '\r\n';
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
            const auto auctionSync = auctionPlayback.sync(
                userinterface::auctionStateReadOnly(), ruleState,
                displayState.desired2DView, displayState.city, *session);
            if (!auctionSync)
                return SDL_SetError("Auction playback: %s", auctionSync.error().c_str());
            const auto pennyBagsSync = auctionPennyBagsPlayback.sync(
                userinterface::auctionState(), ruleState,
                displayState.desired2DView, *session,
                [](std::uint32_t playerMask, std::int64_t serial)
                    -> std::expected<void, std::string>
                {
                    return userinterface::sendReadyResponses(
                        playerMask, serial);
                });
            if (!pennyBagsSync)
                return SDL_SetError("Auction Pennybags playback: %s",
                    pennyBagsSync.error().c_str());
            if (pennyBagsSync->sound)
            {
                const auto& sound = *pennyBagsSync->sound;
                std::expected<void, std::string> played{};
                if (sound.specificOffset)
                {
                    auto* output = audioPlayback();
                    if (output != nullptr)
                    {
                        const auto* choice = udsound::pennybagsChoice(
                            output->boardEdition(), sound.voice);
                        if (choice == nullptr || *sound.specificOffset >= choice->alternateCount)
                            return SDL_SetError("Auction Pennybags specific offset is invalid");
                        const auto wave = data::packDataId(data::LegacyGroupId::LanguageDialog,
                            static_cast<data::DataTag>(choice->firstTag + *sound.specificOffset));
                        played = playPennybagsSpecific(wave,
                            udsound::TokenVoiceClipPolicy::ClipOldSoundIfPlaying, false);
                    }
                }
                else
                {
                    played = playPennybagsComment(sound.voice,
                        udsound::TokenVoiceClipPolicy::ClipOldSoundIfPlaying, false);
                }
                if (!played) return SDL_SetError("Auction Pennybags audio: %s", played.error().c_str());
            }
            if (pennyBagsSync->requestedBackdrop)
                display::setBackdrop(*pennyBagsSync->requestedBackdrop);
            const auto tradeBackdropSync = tradeBackdropPlayback.sync(
                userinterface::tradeStateReadOnly(), ruleState,
                displayState.desired2DView, *session);
            if (!tradeBackdropSync)
                return SDL_SetError("Trade backdrop playback: %s",
                    tradeBackdropSync.error().c_str());
            const auto tradeTokenSync = tradeTokenPlayback.sync(
                userinterface::tradeStateReadOnly(), ruleState,
                displayState.desired2DView, *session);
            if (!tradeTokenSync)
                return SDL_SetError("Trade token playback: %s",
                    tradeTokenSync.error().c_str());
            const auto tradeActionButtonSync = tradeActionButtonPlayback.sync(
                userinterface::tradeStateReadOnly(),
                displayState.desired2DView, *session);
            if (!tradeActionButtonSync)
                return SDL_SetError("Trade action-button playback: %s",
                    tradeActionButtonSync.error().c_str());
            const auto optionsFileSync = optionsFilePlayback.sync(
                userinterface::optionsStateReadOnly(),
                displayState.desired2DView, *session);
            if (!optionsFileSync)
                return SDL_SetError("Options File-screen playback: %s",
                    optionsFileSync.error().c_str());
            const auto chatRecipientSync = chatRecipientPlayback.sync(
                chat::stateReadOnly(), ruleState, *session);
            if (!chatRecipientSync)
                return SDL_SetError("UDChat recipient playback: %s",
                    chatRecipientSync.error().c_str());
            const auto chatOptionSync = chatOptionPlayback.sync(
                chat::stateReadOnly(), *session);
            if (!chatOptionSync)
                return SDL_SetError("UDChat option playback: %s",
                    chatOptionSync.error().c_str());
            const auto chatFluffSync = chatFluffPlayback.sync(
                chat::stateReadOnly(), *session);
            if (!chatFluffSync)
                return SDL_SetError("UDChat fluff playback: %s",
                    chatFluffSync.error().c_str());
            const auto statsSync = statsPlayback.sync(
                userinterface::statsStateReadOnly(),
                displayState.desired2DView, *session);
            if (!statsSync)
                return SDL_SetError("UDStats playback: %s",
                    statsSync.error().c_str());
            const auto statsBankSync = statsBankPlayback.sync(
                userinterface::statsStateReadOnly(), ruleState,
                displayState.desired2DView, *session);
            if (!statsBankSync)
                return SDL_SetError("UDStats Bank playback: %s",
                    statsBankSync.error().c_str());
            const auto statsCalculatorSync = statsCalculatorPlayback.sync(
                displayState.desired2DView,
                userinterface::statsCalculatorStateReadOnly(),
                ruleState, *session);
            if (!statsCalculatorSync)
                return SDL_SetError("UDStats calculator playback: %s",
                    statsCalculatorSync.error().c_str());
            const auto statsCalculatorPickerSync =
                statsCalculatorDeedPickerPlayback.sync(
                    userinterface::statsCalculatorStateReadOnly(),
                    displayState.city, displayState.desired2DView, *session);
            if (!statsCalculatorPickerSync)
                return SDL_SetError("UDStats calculator deed picker: %s",
                    statsCalculatorPickerSync.error().c_str());
            const auto optionsNavigationSync = optionsNavigationPlayback.sync(
                userinterface::optionsStateReadOnly(),
                displayState.desired2DView, *session);
            if (!optionsNavigationSync)
                return SDL_SetError("Options navigation playback: %s",
                    optionsNavigationSync.error().c_str());
            const auto optionsOptionSync = optionsOptionPlayback.sync(
                userinterface::optionsStateReadOnly(),
                displayState.desired2DView, *session);
            if (!optionsOptionSync)
                return SDL_SetError("Options Option-screen playback: %s",
                    optionsOptionSync.error().c_str());
            const auto optionsToggleSync = optionsTogglePlayback.sync(
                userinterface::optionsStateReadOnly(),
                displayState.desired2DView, *session);
            if (!optionsToggleSync)
                return SDL_SetError("Options toggle playback: %s",
                    optionsToggleSync.error().c_str());
            const auto optionsHelpSync = optionsHelpPlayback.sync(
                userinterface::optionsStateReadOnly(),
                displayState.desired2DView, *session);
            if (!optionsHelpSync)
                return SDL_SetError("Options Help-screen playback: %s",
                    optionsHelpSync.error().c_str());
            const auto tradePropertySync = tradePropertyPlayback.sync(
                userinterface::tradeState(), ruleState,
                displayState.desired2DView, tick, *session);
            if (!tradePropertySync)
                return SDL_SetError("Trade deed playback: %s",
                    tradePropertySync.error().c_str());
            const auto tradeIconSync = tradeOfferIconPlayback.sync(
                userinterface::tradeStateReadOnly(),
                displayState.desired2DView, *session);
            if (!tradeIconSync)
                return SDL_SetError("Trade offer-icon playback: %s",
                    tradeIconSync.error().c_str());
            const auto tradeCashDialogSync = tradeCashDialogPlayback.sync(
                userinterface::tradeState(),
                displayState.desired2DView, tick, *session);
            if (!tradeCashDialogSync)
                return SDL_SetError("Trade cash-dialog playback: %s",
                    tradeCashDialogSync.error().c_str());
            const auto tradeContractSync = tradeContractDialogPlayback.sync(
                userinterface::tradeStateReadOnly(),
                displayState.desired2DView, *session);
            if (!tradeContractSync)
                return SDL_SetError("Trade contract-dialog playback: %s",
                    tradeContractSync.error().c_str());
            auto& dicePrompt = userinterface::dicePromptState();
            dicePrompt.show();
            const auto& iBarRules = userinterface::iBarRuleStateReadOnly();
            const rules::PlayerNumber projectedIBarPlayer =
                iBarRules.player < ruleState.numberOfPlayers &&
                iBarRules.player < rules::MaxPlayers
                    ? iBarRules.player
                    : ruleState.currentPlayer;
            const auto effectiveRuleMode = ibar::resolveRuleMode(
                iBarRules.mode, projectedIBarPlayer);
            const rules::PlayerNumber iBarActivePlayer =
                ibar::resolveRulePlayer(projectedIBarPlayer);
            const bool activePlayerCanTrade =
                iBarActivePlayer < ruleState.numberOfPlayers &&
                iBarActivePlayer < rules::MaxPlayers &&
                ruleState.players[iBarActivePlayer].currentSquare < 41;
            const bool tradeEligible = activePlayerCanTrade &&
                ui::localplayers::tradeSourcePlayer(
                    ruleState, iBarActivePlayer) != rules::MaxPlayers;
            const auto bssmAvailability =
                iBarBSSMAvailability(ruleState, iBarActivePlayer);
            statsui::PlayerPlaybackInputs statsPlayerInputs{};
            statsPlayerInputs.mode = effectiveRuleMode;
            statsPlayerInputs.iBarPlayer = iBarActivePlayer;
            statsPlayerInputs.iBarPlayerLocalHuman =
                iBarActivePlayer < rules::MaxPlayers &&
                ui::localplayers::slotIsLocalHumanPlayer(iBarActivePlayer);
            statsPlayerInputs.buildProperties = bssmAvailability.buildProperties;
            statsPlayerInputs.sellProperties = bssmAvailability.sellProperties;
            statsPlayerInputs.mortgageProperties = bssmAvailability.mortgageProperties;
            statsui::setPropertyActionContext(
                userinterface::statsState(), effectiveRuleMode, iBarActivePlayer,
                statsPlayerInputs.iBarPlayerLocalHuman,
                bssmAvailability.buildProperties, bssmAvailability.sellProperties,
                bssmAvailability.mortgageProperties);
            const auto statsPlayerSync = statsPlayerPlayback.sync(
                userinterface::statsStateReadOnly(), ruleState, statsPlayerInputs,
                displayState.desired2DView, *session);
            if (!statsPlayerSync)
                return SDL_SetError("UDStats Player playback: %s",
                    statsPlayerSync.error().c_str());
            const auto statsPlayerCashSync = statsPlayerCashPlayback.sync(
                userinterface::statsStateReadOnly(), ruleState, statsPlayerInputs,
                displayState.desired2DView, *session);
            if (!statsPlayerCashSync)
                return SDL_SetError("UDStats Player cash playback: %s",
                    statsPlayerCashSync.error().c_str());
            const auto statsPlayerAuxSync = statsPlayerAuxPlayback.sync(
                userinterface::statsStateReadOnly(), ruleState, statsPlayerInputs,
                displayState.desired2DView, *session);
            if (!statsPlayerAuxSync)
                return SDL_SetError("UDStats Player aux playback: %s",
                    statsPlayerAuxSync.error().c_str());
            statsui::setFutureImmunityIcons(
                userinterface::statsFutureImmunityState(),
                statsPlayerAuxPlayback.iconHits());
            const auto statsFutureImmunitySync = statsFutureImmunityPlayback.sync(
                userinterface::statsFutureImmunityStateReadOnly(),
                displayState.desired2DView, *session);
            if (!statsFutureImmunitySync)
                return SDL_SetError("UDStats Future/Immunity playback: %s",
                    statsFutureImmunitySync.error().c_str());
            const auto statsDeedSync = statsDeedPlayback.sync(
                userinterface::statsStateReadOnly(), ruleState, statsPlayerInputs,
                displayState.desired2DView, *session);
            if (!statsDeedSync)
                return SDL_SetError("UDStats Deed playback: %s",
                    statsDeedSync.error().c_str());
            const auto statsDeedFloaterSync = statsDeedFloaterPlayback.sync(
                userinterface::statsStateReadOnly(), ruleState, statsPlayerInputs,
                displayState.city, displayState.desired2DView, *session);
            if (!statsDeedFloaterSync)
                return SDL_SetError("UDStats Deed floater playback: %s",
                    statsDeedFloaterSync.error().c_str());
            const auto statsDeedBarSync = statsDeedBarPlayback.sync(
                userinterface::statsStateReadOnly(), ruleState, statsPlayerInputs,
                displayState.desired2DView, *session);
            if (!statsDeedBarSync)
                return SDL_SetError("UDStats Deed owner-bar playback: %s",
                    statsDeedBarSync.error().c_str());
            const auto& iBarState = ibar::stateReadOnly();
            const auto selectedDeed = iBarState.selectedDeed;
            const auto selectedBit = selectedDeed
                ? ibar::layout::propertyBit(*selectedDeed)
                : 0u;

            ibar::ActionButtonInputs iBarInputs{};
            iBarInputs.desired2DView = displayState.desired2DView;
            iBarInputs.ruleMode = effectiveRuleMode;
            iBarInputs.rulePlayer = projectedIBarPlayer;
            iBarInputs.trackRules = !ibar::stateReadOnly().localRuleModeActive;
            iBarInputs.gameInProgress = runtime::state().gameInProgress;
            iBarInputs.tradeEligible = tradeEligible;
            iBarInputs.rollDiceDesired = dicePrompt.currentStartTurn;
            iBarInputs.raiseCashCanBankrupt = iBarRules.raiseCashCanBankrupt;
            const bool deedActive = effectiveRuleMode == ibar::RuleMode::DeedActive;
            iBarInputs.canBuild = deedActive
                ? (bssmAvailability.buildProperties & selectedBit) != 0
                : bssmAvailability.buildProperties != 0;
            iBarInputs.canSell = deedActive
                ? (bssmAvailability.sellProperties & selectedBit) != 0
                : bssmAvailability.sellProperties != 0;
            iBarInputs.canMortgage = deedActive
                ? (bssmAvailability.mortgageProperties & selectedBit) != 0
                : bssmAvailability.mortgageProperties != 0;
            iBarInputs.canUnmortgage = deedActive
                ? (bssmAvailability.unmortgageProperties & selectedBit) != 0
                : bssmAvailability.unmortgageProperties != 0;
            iBarInputs.aiButtonRemoteState =
                effectiveRuleMode == ibar::RuleMode::OtherPlayerRemote
                    ? false
                    : iBarActivePlayer != rules::BankPlayer &&
                      !ui::localplayers::slotIsLocalHumanPlayer(iBarActivePlayer);
            iBarInputs.bankHovered =
                iBarState.playerCurrentMouseOver ==
                    static_cast<int>(rules::BankPlayer);
            iBarInputs.pressedButtonIndex =
                iBarState.pendingPressedButton;
            iBarInputs.desiredCardIndex =
                iBarState.desiredCardIndex;
            iBarInputs.desiredBuyAuctionSquare =
                iBarState.desiredBuyAuctionSquare;
            iBarInputs.desiredBoardCamera = displayState.desiredBoardCamera;

            ibar::ScoreStripInputs scoreInputs{};
            scoreInputs.gameInProgress = runtime::state().gameInProgress;
            scoreInputs.hoveredPlayer = iBarState.playerCurrentMouseOver;
            scoreInputs.tick = static_cast<std::uint32_t>(tick);
            for (std::size_t player = 0; player < iBarState.players.size(); ++player)
                scoreInputs.visiblePlayers[player] = iBarState.players[player].visible;
            const auto scorePlan = ibar::planScoreStrip(ruleState, scoreInputs);
            if (!scorePlan)
                return SDL_SetError("IBar score strip plan: %s", scorePlan.error().c_str());
            iBarInputs.scoreStrip = *scorePlan;

            ibar::PropertyTitleInputs titleInputs{};
            titleInputs.available = iBarVisible &&
                (displayState.desired2DView == display::Screen2D::Main ||
                 displayState.desired2DView == display::Screen2D::Trade);
            titleInputs.player = iBarActivePlayer;
            titleInputs.mode = effectiveRuleMode;
            titleInputs.projectedMode = iBarRules.mode;
            titleInputs.buildProperties = bssmAvailability.buildProperties;
            titleInputs.sellProperties = bssmAvailability.sellProperties;
            titleInputs.mortgageProperties = bssmAvailability.mortgageProperties;
            titleInputs.unmortgageProperties = bssmAvailability.unmortgageProperties;
            titleInputs.freeUnmortgageProperties = iBarRules.freeUnmortgageSet;
            titleInputs.placeBuildingProperties = iBarRules.placeBuildingSet;
            titleInputs.selectedDeed = selectedDeed;
            iBarInputs.propertyTitles = ibar::planPropertyTitles(ruleState, titleInputs);
            ibar::setPropertyHitState(iBarInputs.propertyTitles.visibleProperties);
            iBarInputs.propertyCurrentMouseOver =
                iBarState.propertyCurrentMouseOver;
            iBarInputs.tick = tick;

            const auto actionHitState = ibar::ruleActionHitState(
                iBarVisible, iBarActivePlayer, iBarInputs);
            ibar::setRuleActionHitState(
                actionHitState.layout,
                actionHitState.activeSlots,
                iBarInputs.ruleMode,
                iBarActivePlayer,
                iBarInputs.aiButtonRemoteState);
            const auto previousCardVisualState =
                iBarBackdropPlayback.cardVisualState();
            const auto backdropSync = iBarBackdropPlayback.sync(
                ruleState, iBarVisible, iBarActivePlayer, *session,
                iBarInputs);
            if (!backdropSync)
                return SDL_SetError("IBar backdrop playback: %s",
                    backdropSync.error().c_str());
            if (previousCardVisualState == ibar::CardVisualState::Off &&
                iBarBackdropPlayback.cardVisualState() == ibar::CardVisualState::DeckOut &&
                iBarInputs.desiredCardIndex)
            {
                if (auto* output = audioPlayback())
                {
                    const auto wave = penny::cardReadWave(
                        output->boardEdition(), *iBarInputs.desiredCardIndex);
                    if (wave)
                    {
                        const auto spoken = playPennybagsSpecific(*wave,
                            udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay,
                            false);
                        if (!spoken)
                            return SDL_SetError("Card voice playback: %s",
                                spoken.error().c_str());
                    }
                }
            }
            if (!audioDisabled)
            {
                const auto soundSync = syncMonopolyAudio(ruleState, displayState);
                if (!soundSync)
                    disableAudioPlayback("Monopoly audio playback disabled", soundSync.error());
            }
            if (const auto consumed = iBarBackdropPlayback.consumedPressedButton())
                ibar::clearPendingPressedButton(*consumed);
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
            // UDBoard.cpp uses TokenAnimStackIndex only to choose floating-camera amplitude.
            display::setTokenAnimationStackActive(pieceMovePlayback.active());
            const auto idleSync = syncPieceIdlePlayback(*session);
            if (!idleSync)
                return SDL_SetError("Piece idle playback: %s",
                    idleSync.error().c_str());
            const auto persistentIdleSync =
                syncPersistentPieceIdles(*session, boardVisible);
            if (!persistentIdleSync)
                return SDL_SetError("Persistent piece idle: %s",
                    persistentIdleSync.error().c_str());
            const auto shadowSync = syncPieceShadows(
                *session, ruleState, displayState, boardVisible);
            if (!shadowSync)
                return SDL_SetError("Piece shadow display: %s",
                    shadowSync.error().c_str());
            const auto buildingSync = syncPieceBuildings(*session, boardVisible);
            if (!buildingSync)
                return SDL_SetError("Piece building display: %s",
                    buildingSync.error().c_str());
            const boarddisplay::BoardBackdropInputs backdropInputs{
                displayState.desired2DView, displayState.game3DOn, displayState.city,
                displayState.desiredBoardCamera, static_cast<std::uint32_t>(tick)};
            const auto boardBackdropSync = boardBackdropPlayback.sync(
                backdropInputs, *session);
            if (!boardBackdropSync)
                return SDL_SetError("Board backdrop playback: %s",
                    boardBackdropSync.error().c_str());
            const auto boardSync = syncBoardPlayback(*session, displayState);
            if (!boardSync)
                return SDL_SetError("Board sequence playback: %s",
                    boardSync.error().c_str());
            const auto ownershipPlan = boarddisplay::planOwnershipHighlights(
                ruleState, displayState.desired2DView, displayState.board3DOn,
                displayState.desiredBoardCamera);
            if (!ownershipPlan)
                return SDL_SetError("Board ownership plan: %s",
                    ownershipPlan.error().c_str());
            const auto ownershipSync = ownershipHighlightPlayback.sync(
                *ownershipPlan, *session);
            if (!ownershipSync)
                return SDL_SetError("Board ownership playback: %s",
                    ownershipSync.error().c_str());
            const auto updated = session->update(static_cast<std::int32_t>(tick));
            if (!updated) return SDL_SetError("Sequence playback: %s", updated.error().c_str());
            if (!audioDisabled)
            {
                const auto audioSync = syncSequenceAudio(*session);
                if (!audioSync)
                {
                    disableAudioPlayback("Sequence audio playback disabled", audioSync.error());
                }
            }
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
            const auto lightingSync = syncBoardLighting(
                *session, ruleState, displayState);
            if (!lightingSync)
                return SDL_SetError("Board lighting: %s",
                    lightingSync.error().c_str());
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
        pieceShadowDisplay.reset();
        auctionPlayback.reset();
        auctionPennyBagsPlayback.reset();
        tradeBackdropPlayback.reset();
        tradeTokenPlayback.reset();
        tradeActionButtonPlayback.reset();
        optionsFilePlayback.reset();
        optionsNavigationPlayback.reset();
        statsPlayback.reset();
        chatRecipientPlayback.reset();
        chatOptionPlayback.reset();
        chatFluffPlayback.reset();
        statsBankPlayback.reset();
        statsCalculatorPlayback.reset();
        statsCalculatorDeedPickerPlayback.reset();
        statsPlayerPlayback.reset();
        statsPlayerCashPlayback.reset();
        statsPlayerAuxPlayback.reset();
        statsFutureImmunityPlayback.reset();
        statsDeedPlayback.reset();
        statsDeedFloaterPlayback.reset();
        statsDeedBarPlayback.reset();
        optionsOptionPlayback.reset();
        optionsTogglePlayback.reset();
        optionsHelpPlayback.reset();
        tradePropertyPlayback.reset();
        tradeOfferIconPlayback.reset();
        tradeCashDialogPlayback.reset();
        tradeContractDialogPlayback.reset();
        boardBackdropPlayback.reset();
        ownershipHighlightPlayback.reset();
        boardLightingController.reset();
        lightingTokenPoseTracker = {};
        lastBoardLightingTick = 0;
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
        activePieceMoveToken.reset();
        if (tokenVoiceQueueLockHeld) userinterface::unlockGameQueue();
        tokenVoiceQueueLockHeld = false;
        activeSequenceSounds.clear();
        monopolySoundRuntime.reset(audioRuntime.get());
        audioRuntime.reset();
        audioDisabled = false;
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





