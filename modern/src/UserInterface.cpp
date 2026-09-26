#include "UserInterface.hpp"
#include "UISound.hpp"
#include "Engine.hpp"
#include "Display.hpp"
#include "TimeStep.hpp"
#include "PlayerSelection.hpp"
#include "IBar.hpp"
#include "Timers.hpp"
#include "LocalPlayers.hpp"
#include "PieceCamera.hpp"
#include "RuleArchive.hpp"
#include "Messaging.hpp"
#include "ChatRuntime.hpp"
#include "VoiceChatRuntime.hpp"
#include "UDPennyVoice.hpp"
#include "OptionsSaveRuntime.hpp"
#include "OptionsCustomBoardRuntime.hpp"
#include "OptionsHelpRuntime.hpp"
#include "StatsAccountRuntime.hpp"
#include "ExtendedInitialization.hpp"

#include "RuntimeState.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <bit>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <utility>

namespace monopoly::userinterface
{
    namespace
    {
        dice::PromptState dicePrompt;
        ibar::RuleProjection iBarRuleProjection;
        auctionui::State auctionProjection;
        tradeui::State tradeProjection;
        optionsui::State optionsProjection;
        optionsui::SaveRuntimeState optionsSaveProjection;
        optionsui::CustomBoardState optionsCustomBoardProjection;
        statsui::State statsProjection;
        statsui::CalculatorUIState statsCalculatorProjection;
        statsui::FutureImmunityState statsFutureImmunityProjection;

        constexpr std::array<std::uint32_t, rules::SquareCount> ResyncPropertyBits{{
            0, 1u<<0, 0, 1u<<1, 0, 1u<<2, 1u<<3, 0, 1u<<4, 1u<<5,
            0, 1u<<6, 1u<<7, 1u<<8, 1u<<9, 1u<<10, 1u<<11, 0, 1u<<12, 1u<<13,
            0, 1u<<14, 0, 1u<<15, 1u<<16, 1u<<17, 1u<<18, 1u<<19, 1u<<20, 1u<<21,
            0, 1u<<22, 1u<<23, 0, 1u<<24, 1u<<25, 0, 1u<<26, 0, 1u<<27, 0, 0
        }};

        [[nodiscard]] bool notificationSelectsCurrentUIPlayer(actions::Type action) noexcept
        {
            switch (action)
            {
            case actions::Type::NotifyPleaseRollDice:
            case actions::Type::NotifyBuyOrAuctionDecision:
            case actions::Type::NotifyPleasePay:
            case actions::Type::NotifyJailExitChoice:
            case actions::Type::NotifyPickedUpCard:
            case actions::Type::NotifyFreeUnmortgaging:
            case actions::Type::NotifyFlatOrFractionTaxDecision:
            case actions::Type::NotifyPlaceBuilding:
            case actions::Type::NotifyDecomposeSale:
            case actions::Type::NotifyPlayerBuySellMort:
                return true;
            default:
                return false;
            }
        }

        bool applyClientResyncBlob(
            rules::GameState& state,
            const std::vector<std::uint8_t>& data,
            std::uint8_t* resyncCause = nullptr)
        {
            constexpr std::size_t deckCount =
                static_cast<std::size_t>(rules::DeckType::Count);
            constexpr std::size_t expected = 1 + rules::MaxPlayers * 8 +
                rules::MaxPlayers * 4 + 4 + rules::MaxPlayers + deckCount +
                rules::SquareCount + 1 + 1 + 4 + 1;
            if (data.size() != expected || data[0] != 1) return false;

            std::size_t offset = 1;
            auto readU8 = [&]() { return data[offset++]; };
            auto readU32 = [&]() {
                std::uint32_t value{};
                for (int shift = 0; shift < 32; shift += 8)
                    value |= static_cast<std::uint32_t>(data[offset++]) << shift;
                return value;
            };
            auto readI64 = [&]() {
                std::uint64_t raw{};
                for (int shift = 0; shift < 64; shift += 8)
                    raw |= static_cast<std::uint64_t>(data[offset++]) << shift;
                return std::bit_cast<std::int64_t>(raw);
            };

            rules::GameState next = state;
            for (rules::PlayerNumber player = 0; player < rules::MaxPlayers; ++player)
                next.players[player].cash = readI64();

            std::array<std::uint32_t, rules::MaxPlayers> owned{};
            for (auto& properties : owned) properties = readU32();
            const std::uint32_t mortgaged = readU32();

            for (auto& square : next.squares)
            {
                square.owner = rules::NobodyPlayer;
                square.mortgaged = false;
                square.houses = 0;
            }

            for (std::size_t squareNo = 0; squareNo <= 39; ++squareNo)
            {
                const auto bit = ResyncPropertyBits[squareNo];
                if (bit == 0) continue;
                rules::PlayerNumber owner = rules::NobodyPlayer;
                for (rules::PlayerNumber player = 0; player < rules::MaxPlayers; ++player)
                {
                    if ((owned[player] & bit) == 0) continue;
                    if (owner != rules::NobodyPlayer) return false;
                    owner = player;
                }
                next.squares[squareNo].owner = owner;
                next.squares[squareNo].mortgaged = (mortgaged & bit) != 0;
            }

            for (rules::PlayerNumber player = 0; player < rules::MaxPlayers; ++player)
            {
                const auto square = readU8();
                if (square >= rules::SquareCount) return false;
                next.players[player].currentSquare = square;
            }
            for (std::size_t deck = 0; deck < deckCount; ++deck)
            {
                const auto owner = readU8();
                if (owner > rules::NobodyPlayer) return false;
                next.cards[deck].jailOwner = owner;
            }
            for (std::size_t square = 0; square < rules::SquareCount; ++square)
                next.squares[square].houses = readU8();

            const auto cause = readU8();
            if (cause > 4) return false;
            (void)readU8(); // authoritative RULE phase
            const std::uint32_t firstMoves = readU32();
            for (rules::PlayerNumber player = 0; player < rules::MaxPlayers; ++player)
                next.players[player].firstMoveMade = (firstMoves & (1u << player)) != 0;
            const auto current = readU8();
            // Before a game starts the authoritative state has no current
            // player. Network admission can legitimately resync that state.
            if (current >= rules::MaxPlayers && current != rules::NobodyPlayer)
                return false;
            next.currentPlayer = current;
            state = std::move(next);
            if (resyncCause) *resyncCause = cause;
            return true;
        }
    }
    dice::PromptState& dicePromptState() noexcept { return dicePrompt; }
    const ibar::RuleProjection& iBarRuleStateReadOnly() noexcept
    {
        return iBarRuleProjection;
    }
    auctionui::State& auctionState() noexcept
    {
        return auctionProjection;
    }
    const auctionui::State& auctionStateReadOnly() noexcept
    {
        return auctionProjection;
    }
    tradeui::State& tradeState() noexcept
    {
        return tradeProjection;
    }
    const tradeui::State& tradeStateReadOnly() noexcept
    {
        return tradeProjection;
    }
    optionsui::State& optionsState() noexcept
    {
        return optionsProjection;
    }
    const optionsui::State& optionsStateReadOnly() noexcept
    {
        return optionsProjection;
    }
    optionsui::SaveRuntimeState& optionsSaveState() noexcept
    {
        return optionsSaveProjection;
    }
    const optionsui::SaveRuntimeState& optionsSaveStateReadOnly() noexcept
    {
        return optionsSaveProjection;
    }
    optionsui::CustomBoardState& optionsCustomBoardState() noexcept
    {
        return optionsCustomBoardProjection;
    }
    const optionsui::CustomBoardState& optionsCustomBoardStateReadOnly() noexcept
    {
        return optionsCustomBoardProjection;
    }
    statsui::State& statsState() noexcept
    {
        return statsProjection;
    }
    const statsui::State& statsStateReadOnly() noexcept
    {
        return statsProjection;
    }
    statsui::CalculatorUIState& statsCalculatorState() noexcept
    {
        return statsCalculatorProjection;
    }
    const statsui::CalculatorUIState& statsCalculatorStateReadOnly() noexcept
    {
        return statsCalculatorProjection;
    }
    statsui::FutureImmunityState& statsFutureImmunityState() noexcept
    {
        return statsFutureImmunityProjection;
    }
    const statsui::FutureImmunityState& statsFutureImmunityStateReadOnly() noexcept
    {
        return statsFutureImmunityProjection;
    }
    namespace
    {
        rules::GameState uiRuleState{};
        pieces::PieceMoveIngress pieceMoveIngress;
        pieces::PieceIdleState pieceIdleState;
        dice::Ingress diceIngress;
        std::optional<pieces::PieceIdleTransitionPlan> pendingPieceIdleTransition;
        bool iBarGameJustLoaded = false;
        bool firstNumberOfPlayersNotification = true;
        std::int64_t lastHousingShortageCount = 2;
        std::array<std::uint64_t, rules::MaxPlayers> lastRaiseMoneySoundTick{};
        std::array<std::uint64_t, rules::MaxPlayers> lastBssmBuySoundTick{};
        std::uint64_t lastTradeInitiatorSoundTick{};
        inline constexpr std::uint64_t RaiseMoneyRepeatTicks = 40u * 60u;
        inline constexpr std::uint64_t BssmRepeatTicks = 30u * 60u;
        inline constexpr std::uint64_t TradeInitiatorRepeatTicks = 5u * 60u;

        void completeLoadedGameIBarSetup() noexcept
        {
            if (!iBarGameJustLoaded || iBarRuleProjection.mode == ibar::RuleMode::Nothing)
                return;

            const auto player = iBarRuleProjection.player;
            if (player >= uiRuleState.numberOfPlayers || player >= rules::MaxPlayers)
                return;

            // UDIBar.cpp::UDIBAR_setIBarRulesState: the first non-Nothing
            // RULE state after SetUpLoadedGame owns the center idle and camera.
            iBarGameJustLoaded = false;
            runtime::state().gameInProgress = true;
            display::state().desiredBoardCamera = pieces::pickCameraFor3Squares(
                uiRuleState.players[player].currentSquare);

            // SetUpLoadedGame has already rebuilt every resting slot.  The
            // retail handoff does not animate RestToCenter here: it simply
            // excludes the selected player from resting occupancy and makes it
            // CurrentPlayerInCenterIdle.
            pendingPieceIdleTransition.reset();
            if (!pieceIdleState.initialize(uiRuleState, player))
                pieceIdleState.reset();
        }

        void maybePlayRaiseMoneySuggestion(rules::PlayerNumber player, std::uint64_t tick) noexcept
        {
            if (player >= rules::MaxPlayers)
                return;
            if (tick <= lastRaiseMoneySoundTick[player] + RaiseMoneyRepeatTicks ||
                !display::isIBarVisible(display::stateReadOnly().desired2DView))
                return;
            if (!engine::spokenPostLockSlotEmpty())
                return;
            engine::playPennybagsVoice(udsound::PennybagsVoice::RaisingMoneySuggestion,
                udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay, false);
            lastRaiseMoneySoundTick[player] = tick;
        }

        void maybePlayFirstHouseComment(rules::PlayerNumber player, std::uint64_t tick) noexcept
        {
            if (player >= rules::MaxPlayers ||
                tick <= lastBssmBuySoundTick[player] + BssmRepeatTicks)
                return;
            if (lastBssmBuySoundTick[player] == 0)
                engine::playPennybagsVoice(udsound::PennybagsVoice::PlayerBuiltFirstHouse,
                    udsound::TokenVoiceClipPolicy::SkipIfOldSoundPlaying, false);
            lastBssmBuySoundTick[player] = tick;
        }

        void maybePlayTradeInitiatorComment(std::uint64_t tick) noexcept
        {
            if (!engine::isUsaBoardEdition()) return;
            const auto playerA = tradeProjection.playerA;
            const auto playerB = tradeProjection.playerB;
            if (playerA >= uiRuleState.numberOfPlayers || playerA >= rules::MaxPlayers ||
                playerB >= uiRuleState.numberOfPlayers || playerB >= rules::MaxPlayers ||
                tick <= lastTradeInitiatorSoundTick + TradeInitiatorRepeatTicks)
                return;

            lastTradeInitiatorSoundTick = tick;
            std::optional<udsound::PennybagsVoice> voice;
            if (ui::localplayers::slotIsLocalPlayer(playerB))
            {
                if (uiRuleState.players[playerA].aiPlayerLevel == 0)
                {
                    if (!ui::localplayers::slotIsLocalHumanPlayer(playerA) &&
                        ui::localplayers::slotIsLocalHumanPlayer(playerB))
                        voice = udsound::PennybagsVoice::HumanInitiatesTrade;
                }
                else
                    voice = udsound::PennybagsVoice::AIInitiatesTrade;
            }
            else if (uiRuleState.players[playerA].aiPlayerLevel != 0 &&
                     uiRuleState.players[playerB].aiPlayerLevel != 0)
                voice = udsound::PennybagsVoice::AIInitiatesTrade;

            if (voice)
                engine::playPennybagsVoice(*voice,
                    udsound::TokenVoiceClipPolicy::ClipOldSoundIfPlaying, false);
        }

        void playTokenReaction(rules::PlayerNumber player,
            const std::optional<penny::TokenReaction>& reaction) noexcept
        {
            if (!reaction || player >= uiRuleState.numberOfPlayers ||
                player >= rules::MaxPlayers)
                return;
            engine::playTokenVoice(uiRuleState.players[player].token,
                reaction->line, reaction->policy, reaction->watchAfterStart);
        }

        void playPennybagsReaction(
            const std::optional<penny::PennybagsReaction>& reaction) noexcept
        {
            if (!reaction) return;
            engine::playPennybagsVoice(reaction->voice,
                reaction->policy, reaction->watchAfterStart);
        }

        [[nodiscard]] rules::PlayerNumber chatSender() noexcept
        {
            const auto count = std::min<rules::PlayerNumber>(
                uiRuleState.numberOfPlayers, rules::MaxPlayers);
            if (count == 0) return rules::SpectatorPlayer;
            auto player = uiRuleState.currentPlayer < count
                ? uiRuleState.currentPlayer : static_cast<rules::PlayerNumber>(count - 1u);
            for (rules::PlayerNumber checked = 0; checked < count; ++checked)
            {
                if (ui::localplayers::slotIsLocalHumanPlayer(player)) return player;
                player = player == 0 ? static_cast<rules::PlayerNumber>(count - 1u)
                    : static_cast<rules::PlayerNumber>(player - 1u);
            }
            return rules::SpectatorPlayer;
        }

        [[nodiscard]] std::uint32_t chatEligibleRecipients() noexcept
        {
            std::uint32_t mask{};
            for (rules::PlayerNumber player = 0; player < uiRuleState.numberOfPlayers &&
                 player < rules::MaxPlayers; ++player)
                if (!ui::localplayers::slotIsLocalPlayer(player) &&
                    uiRuleState.players[player].aiPlayerLevel == 0)
                    mask |= 1u << player;
            return mask;
        }

        [[nodiscard]] std::uint32_t localHumanPlayerMask() noexcept
        {
            std::uint32_t mask{};
            const auto count = std::min<rules::PlayerNumber>(
                uiRuleState.numberOfPlayers, rules::MaxPlayers);
            for (rules::PlayerNumber player = 0; player < count; ++player)
                if (ui::localplayers::slotIsLocalHumanPlayer(player))
                    mask |= (1u << player);
            return mask;
        }


        void initializePlayerSetupProjection(
            bool startingNewGame)
        {
            // UDPsel.cpp original, NOTIFY_NUMBER_OF_PLAYERS :
            // l'ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â©tat complet n'est effacÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â© que pour un compteur nul. La
            // premiÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¨re notification non nulle initialise nÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â©anmoins les
            // invariants d'affichage et les joueurs locaux.
            if (startingNewGame)
            {
                uiRuleState = {};
            }


            uiRuleState.options.housesPerHotel =
                5;


            for (auto& square :
                 uiRuleState.squares)
            {
                square.owner =
                    rules::NobodyPlayer;

                square.houses = 0;
            }


            for (auto& player :
                 uiRuleState.players)
            {
                player.currentSquare = 41;
            }
        }
    }

    rules::PlayerNumber chatSenderPlayer() noexcept { return chatSender(); }

    std::expected<void, std::string> sendReadyResponses(
        std::uint32_t playerMask,
        std::int64_t serial)
    {
        const auto count = std::min<rules::PlayerNumber>(
            uiRuleState.numberOfPlayers, rules::MaxPlayers);
        std::size_t required{};
        for (rules::PlayerNumber player = 0; player < count; ++player)
        {
            if ((playerMask & (1u << player)) != 0 &&
                ui::localplayers::slotIsLocalPlayer(player))
            {
                ++required;
            }
        }

        const auto queued = messaging::queuedActionCount();
        if (queued > messaging::MessageQueueCapacity ||
            required > messaging::MessageQueueCapacity - queued)
        {
            return std::unexpected(
                "message queue cannot fit I_AM_HERE responses");
        }

        for (rules::PlayerNumber player = 0; player < count; ++player)
        {
            if ((playerMask & (1u << player)) == 0 ||
                !ui::localplayers::slotIsLocalPlayer(player))
            {
                continue;
            }
            if (!messaging::sendAction(
                    actions::Type::IAmHere, player, rules::BankPlayer, serial))
            {
                return std::unexpected(
                    "validated I_AM_HERE response was rejected");
            }
        }
        return {};
    }


    [[nodiscard]] bool dispatchTradeBatch(
        const std::vector<actions::Message>& batch)
    {
        if (batch.empty()) return true;
        const auto queued = messaging::queuedActionCount();
        if (queued > messaging::MessageQueueCapacity ||
            batch.size() > messaging::MessageQueueCapacity - queued)
            return false;
        for (const auto& action : batch)
            if (!messaging::sendAction(action)) return false;
        return true;
    }


    bool beginTradeFromIBar(rules::PlayerNumber iBarPlayer) noexcept
    {
        if (!runtime::state().gameInProgress ||
            display::state().desired2DView == display::Screen2D::Trade ||
            iBarPlayer >= uiRuleState.numberOfPlayers ||
            iBarPlayer >= rules::MaxPlayers ||
            uiRuleState.players[iBarPlayer].currentSquare >= tradeui::OffBoardSquare)
        {
            return false;
        }

        const auto source = ui::localplayers::tradeSourcePlayer(
            uiRuleState, iBarPlayer);
        if (source == rules::MaxPlayers)
            return false;

        const bool storedTradeValid =
            tradeProjection.tradeFrom < rules::MaxPlayers &&
            tradeProjection.playerA < rules::MaxPlayers &&
            tradeProjection.playerB < rules::MaxPlayers &&
            !tradeProjection.items.empty();
        const bool freshTrade = !storedTradeValid;
        if (!storedTradeValid &&
            !tradeui::beginLocalTrade(tradeProjection, uiRuleState, source))
        {
            return false;
        }
        if (storedTradeValid)
            tradeProjection.ignoreEntryClick = true;

        display::setBackdrop(display::Screen2D::Trade);
        if (freshTrade)
            engine::playPennybagsVoice(
                uiRuleState.numberOfPlayers < 3
                    ? udsound::PennybagsVoice::TradeScreen
                    : udsound::PennybagsVoice::TradeScreen_PickTradePartner,
                udsound::TokenVoiceClipPolicy::ClipOldSoundIfPlaying, false);
        return true;
    }
    bool beginOptionsFromIBar() noexcept
    {
        if (!runtime::state().gameInProgress)
            return false;

        const auto& displayState = display::state();
        auto previousView = displayState.current2DView;
        if (!display::isIBarVisible(previousView))
            previousView = displayState.desired2DView;
        if (!optionsui::beginFromIBar(optionsProjection, previousView))
            return false;

        display::setBackdrop(display::Screen2D::Options);
        return true;
    }



    void resetRuleProjection()
    {
        uiRuleState = {};
        pieceMoveIngress.reset();
        pieceIdleState.reset();
        diceIngress.reset();
        dicePrompt = {};
        iBarRuleProjection.reset();
        auctionui::reset(auctionProjection);
        tradeui::reset(tradeProjection);
        optionsui::reset(optionsProjection);
        optionsSaveProjection = {};
        optionsCustomBoardProjection = {};
        statsui::reset(statsProjection);
        statsui::resetCalculatorUI(statsCalculatorProjection);
        statsui::resetFutureImmunity(statsFutureImmunityProjection);
        chat::reset();
        voicechat::resetSession();
        pendingPieceIdleTransition.reset();
        iBarGameJustLoaded = false;
        firstNumberOfPlayersNotification = true;
        lastHousingShortageCount = 2;
        lastRaiseMoneySoundTick.fill(0);
        display::state().justReadACardHack = false;
    }


    void processRuleMessage(
        const actions::Message& message)
    {
        // ====================================================
        // ProcessMessageToPlayer() boundary.
        // ====================================================

        if (!ui::localplayers::isLocalRecipient(message.toPlayer))
        {
            return;
        }

        // UDIBar.cpp resets the board demo idle timer on every delivered RULE message.
        display::noteBoardActivity();
        if (auto* accounts = engine::statsAccounts())
        {
            if (message.action == actions::Type::NotifyGameStarting)
            {
                if (const auto reset = accounts->resetGame(); !reset)
                    SDL_Log("Stats account reset: %s", reset.error().c_str());
            }
            if (const auto resources = startup::resources())
            {
                const auto recorded = accounts->processRuleMessage(message, uiRuleState,
                    ibar::resolveRulePlayer(iBarRuleProjection.player),
                    display::stateReadOnly().city, *resources);
                if (!recorded) SDL_Log("Stats account history: %s", recorded.error().c_str());
            }
        }

        const bool chatBroadcastTarget = message.numberA >= rules::MaxPlayers;
        const bool chatLocalTarget = message.numberA >= 0 &&
            message.numberA < rules::MaxPlayers &&
            ui::localplayers::slotIsLocalPlayer(
                static_cast<rules::PlayerNumber>(message.numberA));

        if (message.action != actions::Type::NotifyTextChat ||
            chatBroadcastTarget || chatLocalTarget)
        {
            // Userifce.cpp only delivers private text chat to the machine that
            // owns the target slot. Broadcast/spectator targets (>= MaxPlayers)
            // are visible everywhere. message.toPlayer is only the transport
            // recipient and cannot replace this application-level filter.
            chat::setPlayerNames(uiRuleState);
            (void)chat::processRuleMessage(message);
        }

        if (message.action == actions::Type::NotifyVoiceChat &&
            (chatBroadcastTarget || chatLocalTarget))
        {
            // The legacy UI applies the same application-level target filter
            // before handing the RIFF-like voice packet to LE_SOUND_ChatReceive.
            (void)voicechat::processRuleMessage(message);
        }

        // Userifce.cpp retail splits error 71 between host-left warning and
        // the "human replaced by computer" Pennybags comment.
        if (message.action == actions::Type::NotifyErrorMessage && message.numberA == 71)
        {
            if (message.numberC == 6)
                engine::playWarningSound();
            else
                engine::playPennybagsVoice(
                    udsound::PennybagsVoice::HumanReplacedByComputerPlayer,
                    udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay, false);
        }

        if (message.action == actions::Type::NotifyProposedConfiguration)
        {
            rules::GameOptions received = uiRuleState.options;
            if (rules::archive::decodeOptions(message.binaryDataA, received))
                uiRuleState.options = std::move(received);
        }

        if (message.action == actions::Type::NotifyClientResyncInfo)
        {
            std::uint8_t cause{};
            const bool resynced = applyClientResyncBlob(uiRuleState, message.binaryDataA, &cause);
            if (resynced && cause == 2)
            {
                playerselection::recordGameStarted();
                if (auto* accounts = engine::statsAccounts())
                    if (const auto reset = accounts->resetGame(); !reset)
                        SDL_Log("Stats loaded-game history reset: %s", reset.error().c_str());
                runtime::state().gameInProgress = true;
                display::setBackdrop(display::Screen2D::Main);
                ibar::restoreRuleTracking();
                for (auto& hit : uiRuleState.countHits)
                {
                    hit.tradedItem = false;
                    hit.toPlayer = rules::NobodyPlayer;
                }
                pendingPieceIdleTransition.reset();
                if (!pieceIdleState.initialize(uiRuleState))
                    pieceIdleState.reset();
                iBarRuleProjection.reset();
                iBarRuleProjection.player = 0;
                iBarGameJustLoaded = true;
            }
            // LE_SOUND_ChatOn calls ChatOff even when already recording. This
            // announces CHAT to newly admitted peers before subsequent DATN.
            if (resynced)
            {
                engine::stopVoiceChat();
                (void)engine::startVoiceChat();
            }
        }

        dicePrompt.process(message);
        ibar::processRuleMessage(
            message, iBarRuleProjection.mode, timers::tickCount());

        if (message.action == actions::Type::NotifyActionCompleted &&
            message.numberA == static_cast<std::int64_t>(actions::Type::TradeAccept))
        {
            // UDIBar::ActionCompleted restores IBarStateTrackOn for TradeAccept
            // regardless of the accepted/failed result.
            ibar::restoreRuleTracking();
        }

        if (message.action == actions::Type::NotifyActionCompleted &&
            message.numberA == static_cast<std::int64_t>(actions::Type::StartTradeEditing) &&
            message.numberB == 0 &&
            message.numberC >= 0 && message.numberC < rules::MaxPlayers &&
            ui::localplayers::slotIsLocalHumanPlayer(
                static_cast<rules::PlayerNumber>(message.numberC)))
        {
            // UDTrade.cpp warns only on the machine that owns the human
            // whose proposal/edit request was rejected.
            engine::playWarningSound();
        }

        if (message.action == actions::Type::NotifyActionCompleted &&
            message.numberA == static_cast<std::int64_t>(actions::Type::GoBankrupt) &&
            message.numberB == 0)
        {
            // A rejected bankruptcy request keeps the mode and emits Warning.
            engine::playWarningSound();
        }

        if (message.action == actions::Type::NotifyGameStateForSave &&
            optionsSaveProjection.pendingSaveSlot)
        {
            const auto saved = optionsui::persistPendingSave(
                optionsSaveProjection, message.binaryDataA);
            if (!saved) engine::playWarningSound();
        }

        if (message.action == actions::Type::NotifyActionCompleted &&
            message.numberA == static_cast<std::int64_t>(actions::Type::GetGameStateForSave) &&
            message.numberB == 0 &&
            message.numberC >= 0 && message.numberC < rules::MaxPlayers &&
            ui::localplayers::slotIsLocalPlayer(
                static_cast<rules::PlayerNumber>(message.numberC)))
        {
            // A rejected retail save request cannot leave a stale pending slot:
            // no NOTIFY_GAME_STATE_FOR_SAVE will follow this failed action.
            optionsSaveProjection.pendingSaveSlot.reset();
            optionsSaveProjection.pendingMetadata = {};
            // UDIBar uses WAV_tmpnext here, deliberately not the generic warning.
            engine::playSaveFailureSound();
        }

        if (notificationSelectsCurrentUIPlayer(message.action) &&
            message.numberA >= 0 && message.numberA <= rules::NobodyPlayer)
        {
            ui::localplayers::setCurrentUIPlayerFromPlayerNumber(
                static_cast<rules::PlayerNumber>(message.numberA));
        }

        if (message.action == actions::Type::NotifyFreeUnmortgaging &&
            message.numberB != 0)
            ibar::restoreRuleTracking();

        if (message.action == actions::Type::NotifyPleasePay &&
            message.numberA >= 0 && message.numberA < rules::MaxPlayers)
            maybePlayRaiseMoneySuggestion(
                static_cast<rules::PlayerNumber>(message.numberA), timers::tickCount());
        if (message.action == actions::Type::NotifyJailExitChoice &&
            message.numberA >= 0 && message.numberA < rules::MaxPlayers &&
            message.numberB != 0 &&
            ui::localplayers::slotIsLocalHumanPlayer(
                static_cast<rules::PlayerNumber>(message.numberA)))
            engine::playJailChoiceHostComment();

        if (message.action == actions::Type::NotifyCashAmount &&
            message.numberA >= 0 && message.numberA < rules::MaxPlayers)
            uiRuleState.players[static_cast<std::size_t>(message.numberA)].cash =
                message.numberC;

        if (message.action == actions::Type::NotifySquareOwnership &&
            message.numberA >= 0 && message.numberA < rules::SquareCount &&
            message.numberB >= 0 && message.numberB <= rules::EscrowPlayer)
            uiRuleState.squares[static_cast<std::size_t>(message.numberA)].owner =
                static_cast<rules::PlayerNumber>(message.numberB);

        if (message.action == actions::Type::NotifySquareMortgage &&
            message.numberA >= 0 && message.numberA < rules::SquareCount)
        {
            auto& square = uiRuleState.squares[static_cast<std::size_t>(message.numberA)];
            square.mortgaged = message.numberB != 0;
            if (square.owner < rules::MaxPlayers)
            {
                engine::playClickSound();
                display::requestBssmCamera(static_cast<std::int32_t>(message.numberA),
                    message.numberB != 0 ? 2U : 3U);
            }
        }

        if (message.action == actions::Type::NotifySquareHouses &&
            message.numberA >= 0 && message.numberA < rules::SquareCount &&
            message.numberB >= 0 && message.numberB <= 255 &&
            message.numberC >= 0 && message.numberC <= 255)
        {
            auto& square = uiRuleState.squares[static_cast<std::size_t>(message.numberA)];
            const auto previousHouses = square.houses;
            square.houses = static_cast<std::uint8_t>(message.numberB);
            uiRuleState.options.housesPerHotel = static_cast<std::uint8_t>(message.numberC);
            if (square.owner < rules::MaxPlayers)
            {
                if (previousHouses < square.houses)
                {
                    engine::playBuildSound();
                    display::requestBssmCamera(static_cast<std::int32_t>(message.numberA), 0U);
                }
                else
                {
                    engine::playUnbuildSound();
                    display::requestBssmCamera(static_cast<std::int32_t>(message.numberA), 1U);
                }
            }
            if (previousHouses < square.houses)
                maybePlayFirstHouseComment(square.owner, timers::tickCount());
        }

        if (message.action == actions::Type::NotifyFreeParkingPot)
            uiRuleState.freeParkingJackpotAmount = message.numberA;

        if (message.action == actions::Type::NotifyJailCardOwnership &&
            message.numberA >= 0 && message.numberA <= rules::NobodyPlayer &&
            message.numberB >= 0 &&
            message.numberB < static_cast<std::int64_t>(rules::DeckType::Count))
        {
            uiRuleState.cards[static_cast<std::size_t>(message.numberB)].jailOwner =
                static_cast<rules::PlayerNumber>(message.numberA);
        }

        if ((message.action == actions::Type::NotifyImmunityCount ||
             message.action == actions::Type::NotifyFutureRentCount) &&
            message.numberA >= 0 && message.numberA < rules::MaxPlayers &&
            message.numberD >= 0 && message.numberD < rules::MaxPlayers &&
            message.numberB >= std::numeric_limits<std::int32_t>::min() &&
            message.numberB <= std::numeric_limits<std::int32_t>::max() &&
            message.numberE >= 0 &&
            message.numberE <= std::numeric_limits<std::uint32_t>::max())
        {
            (void)tradeui::addUiImmunity(
                uiRuleState,
                static_cast<rules::PlayerNumber>(message.numberD),
                static_cast<rules::PlayerNumber>(message.numberA),
                message.action == actions::Type::NotifyFutureRentCount
                    ? rules::CountHitType::FutureRent
                    : rules::CountHitType::RentImmunity,
                static_cast<std::int32_t>(message.numberB),
                static_cast<std::uint32_t>(message.numberE), false);
        }

        if (message.action == actions::Type::NotifyActionCompleted &&
            message.numberB != 0 &&
            message.numberA == static_cast<std::int64_t>(actions::Type::CardSeen))
        {
            display::state().justReadACardHack = true;
        }

        if (message.action == actions::Type::NotifyBuyOrAuctionDecision &&
            message.numberA >= 0 && message.numberA < uiRuleState.numberOfPlayers &&
            message.numberA < rules::MaxPlayers)
        {
            const auto player = static_cast<rules::PlayerNumber>(message.numberA);
            playPennybagsReaction(penny::buyOrAuctionPennybagsReaction(uiRuleState, player));
        }

        if (message.action == actions::Type::NotifyActionCompleted &&
            message.numberB != 0 &&
            message.numberA == static_cast<std::int64_t>(actions::Type::BuyOrAuctionDecision) &&
            message.numberC >= 0 && message.numberC < uiRuleState.numberOfPlayers &&
            message.numberC < rules::MaxPlayers)
        {
            const auto player = static_cast<rules::PlayerNumber>(message.numberC);
            const auto property = uiRuleState.players[player].currentSquare;
            if (message.numberD != 0)
                playPennybagsReaction(penny::boughtPropertyPennybagsReaction(
                    uiRuleState, player, property));
            const auto reaction = message.numberD != 0
                ? penny::boughtPropertyReaction(uiRuleState, player, property)
                : penny::choseAuctionReaction(uiRuleState, player);
            playTokenReaction(player, reaction);
        }
        if (message.action == actions::Type::NotifyAreYouThere &&
            message.numberC != static_cast<std::int64_t>(actions::Type::NotifyNewHighBid))
        {
            // Userifce.cpp::Process_NOTIFY_ARE_YOU_THERE responds immediately
            // for ordinary roll-calls. Auction readiness remains deliberately
            // deferred until its Pennybags/graphics intro reaches Begin.
            (void)sendReadyResponses(static_cast<std::uint32_t>(message.numberA),
                message.numberB);
        }

        const auto auctionUpdate = auctionui::processRuleMessage(
            auctionProjection, uiRuleState, message, display::state().desired2DView);
        if (auctionUpdate.requestedBackdrop)
            display::setBackdrop(*auctionUpdate.requestedBackdrop);
        if (message.action == actions::Type::NotifyNewHighBid &&
            message.numberA == rules::BankPlayer)
        {
            // UDAuct.cpp clears the RULE button bar for every bank high-bid
            // notification, including a zero-bid gong while Auction is visible.
            iBarRuleProjection.mode = ibar::RuleMode::Nothing;
            iBarRuleProjection.player = uiRuleState.currentPlayer;
        }
        if (message.action == actions::Type::NotifyHousingShortage)
        {
            rules::PlayerNumber originalBuyer = rules::NobodyPlayer;
            if (message.numberA >= 0 && message.numberA < rules::MaxPlayers)
                originalBuyer = static_cast<rules::PlayerNumber>(message.numberA);
            const std::uint32_t allowedPlayers = message.numberE > 0
                ? static_cast<std::uint32_t>(message.numberE)
                : 0u;
            const auto shortagePlayer = ui::localplayers::housingShortageIBarPlayer(
                uiRuleState, originalBuyer, allowedPlayers);
            iBarRuleProjection.processHousingShortage(message, shortagePlayer);
            if (shortagePlayer < rules::MaxPlayers)
                ibar::restoreRuleTracking();
            if (shortagePlayer < rules::MaxPlayers &&
                ui::localplayers::slotIsLocalHumanPlayer(shortagePlayer))
            {
                if (message.numberD == 0 && lastHousingShortageCount != message.numberD)
                    engine::playPennybagsVoice(
                        udsound::PennybagsVoice::HousingShortage_NotFirstTime,
                        udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay, false);
                lastHousingShortageCount = message.numberD;
            }
        }
        else
        {
            if (message.action == actions::Type::NotifyTradeEditor &&
                message.numberA >= 0 && message.numberA < rules::MaxPlayers)
            {
                // UDTrade.cpp selects the editor before deciding whether this
                // machine owns that slot, leaving Nobody for remote editors.
                ui::localplayers::setCurrentUIPlayerFromPlayerNumber(
                    static_cast<rules::PlayerNumber>(message.numberA));
            }

            iBarRuleProjection.process(message);
            if (message.action == actions::Type::NotifyTradeAcceptanceDecision)
            {
                // Retail ignores numberA for CurrentUIPlayer here and selects
                // exactly the reconstructed TradeB slot.  Spectators therefore
                // keep Nobody even though the IBar may watch a remote pending
                // player from numberA.
                const auto tradeBPlayer = iBarRuleProjection.tradeBPlayer;
                const std::uint32_t tradeBSet = tradeBPlayer < rules::MaxPlayers
                    ? (1u << tradeBPlayer)
                    : 0u;
                ui::localplayers::setCurrentUIPlayerFromPlayerSet(
                    uiRuleState, tradeBSet);

                const std::uint32_t pendingPlayers = message.numberA > 0
                    ? static_cast<std::uint32_t>(message.numberA)
                    : 0u;
                const auto tradePlayer = ui::localplayers::tradeAcceptanceIBarPlayer(
                    uiRuleState, tradeBPlayer, pendingPlayers);
                iBarRuleProjection.processTradeAcceptance(message, tradePlayer);
            }
        }

        if (message.action == actions::Type::NotifyTradeFinished)
        {
            // UDTrade.cpp records Nothing/CurrentPlayer as the RULE baseline,
            // then only restores tracking when CurrentPlayer == numberB.
            // Otherwise it explicitly shows the current player through the
            // local OtherPlayer/OtherPlayerRemote override.
            iBarRuleProjection.mode = ibar::RuleMode::Nothing;
            iBarRuleProjection.player = uiRuleState.currentPlayer;
            if (message.numberB ==
                static_cast<std::int64_t>(uiRuleState.currentPlayer))
            {
                ibar::restoreRuleTracking();
            }
            else
            {
                ibar::inspectPlayer(uiRuleState.currentPlayer);
            }
        }

        completeLoadedGameIBarSetup();

        const auto tradeUpdate = tradeui::processRuleMessage(
            tradeProjection, uiRuleState, message,
            display::state().desired2DView, localHumanPlayerMask());
        if (tradeUpdate.requestedBackdrop)
            display::setBackdrop(*tradeUpdate.requestedBackdrop);
        if (message.action == actions::Type::NotifyTradeAcceptanceDecision)
            maybePlayTradeInitiatorComment(timers::tickCount());
        if (message.action == actions::Type::NotifyTradeFinished &&
            message.numberA == 1)
            engine::playPennybagsVoice(
                udsound::PennybagsVoice::TradeScreen_TradeComplete,
                udsound::TokenVoiceClipPolicy::ClipOldSoundIfPlaying, false);
        else if (message.action == actions::Type::NotifyTradeFinished &&
                 message.numberA == 0)
            engine::playPennybagsVoice(
                udsound::PennybagsVoice::TradeScreen_TradeCancelled,
                udsound::TokenVoiceClipPolicy::ClipOldSoundIfPlaying, false);
        if (message.action == actions::Type::NotifyTradeEditor &&
            message.numberA >= 0 && message.numberA < rules::MaxPlayers)
        {
            const auto batch = tradeui::planEditorSubmission(
                tradeProjection, static_cast<rules::PlayerNumber>(message.numberA),
                localHumanPlayerMask());
            (void)dispatchTradeBatch(batch);
        }

        if (message.action == actions::Type::NotifyDiceRolled)
        {
            const auto roll = diceIngress.process(
                uiRuleState, message, timers::tickCount());
            if (roll) lockGameQueue();
        }

        if (message.action == actions::Type::NotifyMoveForwards ||
            message.action == actions::Type::NotifyMoveBackwards ||
            message.action == actions::Type::NotifyJumpToSquare)
        {
            // UDIBar.cpp processes the move against the old UI square, then
            // updates UICurrentGameState except for the GoToJail destination.
            const auto movement = pieceMoveIngress.process(
                uiRuleState, message, display::stateReadOnly().optionTokenAnimationsOn);
            if (movement && movement->sourceQueueLockRequired)
                lockGameQueue();
            if (movement && movement->projectionUpdated &&
                tradeui::abortIfParticipantOffBoard(
                    tradeProjection, uiRuleState, display::state().desired2DView))
            {
                display::setBackdrop(display::Screen2D::Main);
            }

            if (movement && message.numberA == 40 &&
                message.numberC >= 0 && message.numberC < uiRuleState.numberOfPlayers &&
                message.numberC < rules::MaxPlayers)
            {
                const auto player = static_cast<rules::PlayerNumber>(message.numberC);
                const bool localHuman = ui::localplayers::slotIsLocalHumanPlayer(player);
                playPennybagsReaction(penny::goToJailPennybagsReaction(
                    uiRuleState, player, localHuman));
                playTokenReaction(player, penny::goToJailReaction(
                    uiRuleState, player, localHuman));
            }
        }

        if (message.action == actions::Type::NotifyEndTurn &&
            message.numberA >= 0 && message.numberA < uiRuleState.numberOfPlayers)
        {
            const auto player = static_cast<rules::PlayerNumber>(message.numberA);
            uiRuleState.currentPlayer = player;
            ui::localplayers::setCurrentUIPlayerFromPlayerNumber(player);
            display::state().flashCurrentToken = true;
        }

        if (message.action == actions::Type::NotifyStartTurn &&
            message.numberA >= 0 &&
            message.numberA < uiRuleState.numberOfPlayers)
        {
            const auto newCurrent = static_cast<rules::PlayerNumber>(message.numberA);
            const auto random100 = static_cast<std::uint32_t>(std::rand() % 100);
            std::optional<std::uint32_t> random8;
            if (uiRuleState.players[newCurrent].firstMoveMade &&
                uiRuleState.players[newCurrent].aiPlayerLevel != 0)
                random8 = static_cast<std::uint32_t>(std::rand() % 8);
            if (const auto reactions = penny::nextPlayerReactions(
                    uiRuleState, newCurrent, random100, random8))
            {
                playPennybagsReaction(reactions->host);
                playTokenReaction(newCurrent, reactions->token);
            }
            display::state().desiredBoardCamera = pieces::pickCameraFor3Squares(
                uiRuleState.players[newCurrent].currentSquare);
            if (!pendingPieceIdleTransition)
            {
                if (auto plan = pieceIdleState.planTurnChange(uiRuleState, newCurrent))
                {
                    pendingPieceIdleTransition = std::move(*plan);
                    lockGameQueue();
                }
            }
            // UDIBar.cpp assigns CurrentPlayer only after the idle plan and lock.
            runtime::state().gameInProgress = true;
            runtime::state().gamePaused = false;
            uiRuleState.currentPlayer = newCurrent;
            ui::localplayers::setCurrentUIPlayerFromPlayerNumber(newCurrent);
            display::state().flashCurrentToken = true;
        }

        if (message.action == actions::Type::NotifyPleaseRollDice)
        {
            // UDIBar.cpp:3397-3403 uses the notification player directly.
            if (message.numberA >= 0 && message.numberA < uiRuleState.numberOfPlayers &&
                message.numberA < rules::MaxPlayers)
            {
                const auto player = static_cast<rules::PlayerNumber>(message.numberA);
                auto& displayState = display::state();
                displayState.desiredBoardCamera = pieces::selectAppropriateView(
                    pieces::BoardViewSelectionType::RollDice,
                    displayState.desiredBoardCamera,
                    uiRuleState.players[player].currentSquare,
                    0);
            }
            // UDIBar.cpp sets GameInProgress before leaving the roll prompt.
            runtime::state().gameInProgress = true;
            runtime::state().gamePaused = false;
        }

        if (message.action == actions::Type::NotifyJailExitChoice)
        {
            // UDIBar.cpp:3640-3643 always selects VIEW2D17_CORNER_JAIL.
            auto& displayState = display::state();
            displayState.desiredBoardCamera = pieces::selectAppropriateView(
                pieces::BoardViewSelectionType::JailChoice,
                displayState.desiredBoardCamera,
                0,
                1);
        }

        if (
            message.action ==
            actions::Type::NotifyNumberOfPlayers)
        {
            const std::int64_t count =
                std::clamp<std::int64_t>(
                    message.numberA,
                    0,
                    static_cast<std::int64_t>(
                        rules::MaxPlayers
                    )
                );


            const bool initializeProjection =
                count == 0 ||
                firstNumberOfPlayersNotification;


            if (initializeProjection)
            {
                firstNumberOfPlayersNotification = false;
                runtime::state().gameInProgress = false;


                initializePlayerSetupProjection(
                    count == 0
                );


                // Original UDPSEL reset :
                //
                // NumberOfLocalPlayers = 0;
                // SlotIsALocal* = FALSE;
                // LocalPlayerSlots = NOBODY.
                ui::localplayers::reset();
            }


            uiRuleState.numberOfPlayers =
                static_cast<rules::PlayerNumber>(
                    count
                );
        }


        // CheckForAcceptingOurNewPlayer() doit prÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â©cÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â©der
        // UDPSEL_ProcessMessageToPlayer().
        ui::localplayers::processRuleMessage(
            uiRuleState,
            message
        );


        switch (message.action)
        {
            case actions::Type::NotifyGameStarting:
            {
                // Userifce.cpp original first spreads every token across GO
                // in reverse player order, with no current center idle.
                pendingPieceIdleTransition.reset();
                if (!pieceIdleState.initializeNewGame(uiRuleState))
                    pieceIdleState.reset();
                for (auto& hit : uiRuleState.countHits)
                {
                    hit.tradedItem = false;
                    hit.toPlayer = rules::NobodyPlayer;
                }
                ibar::restoreRuleTracking();
                // Userifce.cpp original :
                // UDPSEL_GameHasJustStarted();
                // UDBOARD_SetBackdrop(DISPLAY_SCREEN_MainA);
                //
                // La persistance du journal de joueurs effectuee par
                // UDPSEL_GameHasJustStarted() n'a pas encore de backend
                // portable. La transition d'ecran, elle, est exacte et
                // reste differee jusqu'au prochain show DISPLAY.
                display::setBackdrop(display::Screen2D::Main);
                // Retail starts capture here only when MESS_NetworkMode is true.
                (void)engine::startVoiceChat();
                break;
            }


            case actions::Type::NotifyGameOver:
            {
                if (runtime::state().gameInProgress)
                {
                    runtime::state().gameInProgress = false;
                    ui::localplayers::setCurrentUIPlayerFromPlayerSet(
                        uiRuleState, 0xFFFFFFFFu);
                    engine::playPennybagsVoice(udsound::PennybagsVoice::PlayAgain,
                        udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay, false);
                    display::setBackdrop(display::Screen2D::Main);
                }
                break;
            }


            case actions::Type::NotifyGamePaused:
            {
                runtime::state().gamePaused = true;
                break;
            }


            default:
                break;
        }


        if (statsProjection.initialized &&
            display::stateReadOnly().desired2DView == display::Screen2D::Portfolio)
        {
            // UDStats content is live: cash/ownership/building notifications
            // invalidate the active sort while the Portfolio is visible.
            statsui::refresh(statsProjection, uiRuleState);
        }

        if (statsFutureImmunityProjection.open)
            statsui::refreshFutureImmunity(
                statsFutureImmunityProjection, uiRuleState);

        playerselection::processMessage(
            message
        );
    }


    rules::GameState& ruleState()
    {
        return uiRuleState;
    }


    const rules::GameState& ruleStateReadOnly()
    {
        return uiRuleState;
    }

    const pieces::PieceIdleState& pieceIdleStateReadOnly()
    {
        return pieceIdleState;
    }

    std::optional<pieces::PieceMovePlan> takePendingPieceMovePlan()
    {
        return pieceMoveIngress.takePlan();
    }

    std::optional<pieces::PieceMoveSpecialRequest> takePendingPieceMoveSpecial()
    {
        return pieceMoveIngress.takeSpecial();
    }
    std::optional<pieces::PieceIdleTransitionPlan> takePendingPieceIdleTransitionPlan()
    {
        auto result = std::move(pendingPieceIdleTransition);
        pendingPieceIdleTransition.reset();
        return result;
    }

    std::optional<dice::RollRequest> takePendingDiceRoll()
    {
        return diceIngress.take();
    }

    void update()
    {
        statsui::syncView(
            statsProjection, uiRuleState,
            display::stateReadOnly().desired2DView);
        statsui::syncCalculatorView(
            statsCalculatorProjection,
            display::stateReadOnly().desired2DView);
        statsui::syncFutureImmunityView(
            statsFutureImmunityProjection, statsProjection.screen,
            display::stateReadOnly().desired2DView);

        // ProcessPlayersUI(NULL) original entretient les effets UI
        // periodiques, mais ne valide pas une phase UDPSEL. Le commit
        // desired/current appartient exclusivement a DISPLAY_UDPSEL_Show().
        const auto player = ibar::resolveRulePlayer(iBarRuleProjection.player);
        if (ibar::resolveRuleMode(iBarRuleProjection.mode, iBarRuleProjection.player) ==
                ibar::RuleMode::RaiseMoney && player < rules::MaxPlayers)
            maybePlayRaiseMoneySuggestion(player, timers::tickCount());
    }

    bool processUIMessage(const uimsg::Message& message)
    {
        chat::setPlayerNames(uiRuleState);
        // ProcessLibraryMessage() original appelle
        // AdvanceTimeStep() ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â  chaque message ArtLib.
        advanceTimeStep();

        if (chat::processInput(
                message, chatSender(), chatEligibleRecipients(),
                messaging::networkMode()))
        {
            update();
            return !runtime::state().gameQuitRequested;
        }

        // ProcessLibraryMessage() original distribue ensuite
        // le message aux modules UD actifs.
        //
        // Ordre historique des modules interactifs portes ici :
        // UDAUCT_ProcessMessage, UDBOARD_ProcessMessage, UDIBAR_ProcessMessage,
        // UDPSEL_ProcessMessage puis UDTRADE_ProcessMessage.
        if (const auto bid = auctionui::planBid(
                auctionProjection, uiRuleState.numberOfPlayers,
                display::state().desired2DView, message, localHumanPlayerMask()))
        {
            actions::Message action{};
            action.action = actions::Type::Bid;
            action.fromPlayer = bid->player;
            action.toPlayer = rules::BankPlayer;
            action.numberA = bid->amount;
            (void)messaging::sendAction(action);
        }
        display::processBoardInput(message);
        const bool exitCreditsWereActive = ibar::stateReadOnly().userChoseToExit;
        const bool optionPreviewWasActive = optionsProjection.active &&
            optionsProjection.currentScreen == optionsui::Screen::Option &&
            optionsProjection.optionSnapshotLoaded;
        const auto originalMusicTune = optionsProjection.originalMusicTuneIndex;
        ibar::processLibraryMessage(
            message
        );
        const bool exitCreditsConsumed = exitCreditsWereActive ||
            ibar::stateReadOnly().userChoseToExit;
        const auto customBoardInput = exitCreditsConsumed ? optionsui::CustomBoardInput{} :
            optionsui::processCustomBoardInput(optionsCustomBoardProjection, message);
        const auto saveInput = (customBoardInput.consumed || exitCreditsConsumed)
            ? optionsui::SaveDialogInput{}
            : optionsui::processSaveDialogInput(optionsSaveProjection, message,
                engine::fontPlayback(), startup::resources());
        optionsui::InputResult optionsInput{};
        if (!customBoardInput.consumed && !saveInput.consumed && !exitCreditsConsumed)
            optionsInput = optionsui::processInput(
                optionsProjection, display::state().desired2DView, message);
        if (optionPreviewWasActive && !optionsInput.pressedOptionOkay &&
            (!optionsProjection.active || !optionsProjection.optionSnapshotLoaded ||
             optionsProjection.currentScreen != optionsui::Screen::Option))
            display::applyMusicTune(originalMusicTune);
        if (customBoardInput.playClick) engine::playClickSound();
        if (saveInput.playClick) engine::playClickSound();
        const bool optionClicked = optionsInput.pressedMenuButton.has_value() ||
            optionsInput.pressedFileButton.has_value() ||
            optionsInput.pressedHelpButton.has_value() ||
            optionsInput.pressedOptionToggle.has_value() ||
            optionsInput.pressedMusicTune.has_value() ||
            optionsInput.pressedOptionOkay;
        if (optionClicked) engine::playClickSound();
        if (optionsInput.pressedMusicTune)
            display::applyMusicTune(optionsProjection.musicTuneIndex);
        if (optionsInput.pressedHelpButton == optionsui::HelpButton::QuickHelp ||
            optionsInput.pressedHelpButton == optionsui::HelpButton::FullHelp)
        {
            const auto resources = startup::resources();
            const auto opened = resources
                ? (optionsInput.pressedHelpButton == optionsui::HelpButton::QuickHelp
                    ? optionsui::openQuickHelp(optionsProjection, *resources)
                    : optionsui::openFullHelp(*resources))
                : std::expected<void, std::string>(std::unexpected("Help resources unavailable"));
            if (!opened) engine::playWarningSound();
        }

        if (customBoardInput.requestLoad)
        {
            const auto selection = optionsui::validateSelectedCustomBoard(
                optionsCustomBoardProjection);
            const auto committed = selection
                ? playerselection::commitCustomBoard(selection->assetRoot)
                : std::expected<void, std::string>(
                    std::unexpected(selection.error()));
            if (committed)
            {
                const auto previous = optionsCustomBoardProjection.previousView;
                optionsui::closeCustomBoardDialog(optionsCustomBoardProjection);
                optionsProjection.active = false;
                display::setBackdrop(previous);
            }
            else engine::playWarningSound();
        }
        else if (customBoardInput.closeDialog)
        {
            const auto previous = optionsCustomBoardProjection.previousView;
            optionsui::closeCustomBoardDialog(optionsCustomBoardProjection);
            optionsProjection.active = false;
            display::setBackdrop(previous);
        }

        bool loadGameDispatched = false;
        bool retainLoadDialog = false;
        if (saveInput.requestLoad)
        {
            const auto blob = optionsui::readSelectedGameBlob(optionsSaveProjection);
            const auto localPlayer = ui::localplayers::anyLocalPlayer(uiRuleState);
            bool sent = false;
            int loadedCity = display::stateReadOnly().city;
            int loadedSystem = display::stateReadOnly().system;
            std::filesystem::path loadedCustomRoot;
            bool customBoardRemoved = false;
            bool boardReady = true;
            // RuleSave accepts an unassigned sender on an empty local/server
            // game, including Load from player selection before a slot exists.
            const bool mayLoad = localPlayer < rules::MaxPlayers ||
                (uiRuleState.numberOfPlayers == 0 && messaging::serverMode());
            if (blob && mayLoad)
            {
                const auto& metadata = optionsSaveProjection.slots[
                    static_cast<std::size_t>(optionsSaveProjection.selectedSlot)].metadata;
                loadedCity = metadata.city;
                loadedSystem = metadata.system;
                if (loadedCity < 0)
                {
                    const auto resources = startup::resources();
                    if (!resources) boardReady = false;
                    else
                    {
                        const bool usa = resources->context().board == data::BoardEdition::Usa;
                        if (usa) loadedSystem = 13;
                        const auto restored = optionsui::restoreSavedCustomBoard(
                            metadata.customBoardName, *resources, loadedSystem);
                        boardReady = restored.has_value();
                        if (restored && *restored)
                        {
                            loadedCity = -1;
                            loadedCustomRoot = **restored;
                        }
                        else if (restored)
                        {
                            // SetUpLoadedGame falls back only when the saved
                            // first camera is missing (the board was removed).
                            const int language = static_cast<int>(resources->context().language);
                            loadedCity = usa || language < 2 || language > 10 ? 0 : language - 2;
                            customBoardRemoved = true;
                        }
                    }
                }
                actions::Message action{};
                action.action = actions::Type::SetGameState;
                action.fromPlayer = localPlayer;
                action.toPlayer = rules::BankPlayer;
                action.numberB = 1; // retail load-game marker
                action.numberC = 1; // immunity/future save version
                action.binaryDataA = *blob;
                if (boardReady) sent = messaging::sendAction(action);
            }
            if (sent)
            {
                loadGameDispatched = true;
                auto& displayState = display::state();
                const auto metadata = optionsui::applySelectedMetadata(
                    optionsSaveProjection, uiRuleState,
                    displayState.city, displayState.system);
                if (!metadata)
                {
                    engine::playWarningSound();
                }
                else
                {
                    displayState.city = loadedCity;
                    displayState.system = loadedSystem;
                    displayState.customBoardPath = std::move(loadedCustomRoot);
                    if (customBoardRemoved) engine::playWarningSound();
                }
                // SetUpLoadedGame() leaves Options immediately; the RULE resync
                // that follows completes the loaded-game projection.
                optionsProjection.active = false;
                display::setBackdrop(display::Screen2D::Main);
            }
            else
            {
                engine::playWarningSound();
                retainLoadDialog = true;
            }
        }

        if (saveInput.requestSave)
        {
            const auto localPlayer = ui::localplayers::anyLocalPlayer(uiRuleState);
            const auto& displayState = display::stateReadOnly();
            const auto customPath = displayState.customBoardPath.u8string();
            const auto prepared = optionsui::beginPendingSave(
                optionsSaveProjection, uiRuleState,
                displayState.city, displayState.system,
                std::string(reinterpret_cast<const char*>(customPath.data()), customPath.size()));
            const bool sent = prepared && localPlayer < rules::MaxPlayers &&
                messaging::sendAction(actions::Type::GetGameStateForSave,
                    localPlayer, rules::BankPlayer);
            if (!sent)
            {
                optionsSaveProjection.pendingSaveSlot.reset();
                optionsSaveProjection.pendingMetadata = {};
                engine::playWarningSound();
            }
        }

        if (saveInput.closeDialog && !retainLoadDialog)
        {
            optionsui::closeSaveDialog(optionsSaveProjection);
            if (!loadGameDispatched)
                optionsProjection.currentScreen = optionsui::Screen::File;
        }

        if (optionsInput.pressedFileButton == optionsui::FileButton::NewGame)
        {
            // UDOPTIONS_ProcessFileOptionButtonPress sets the request flag,
            // removes the File screen and fakes Escape into UDIBAR.
            optionsProjection.active = false;
            (void)ibar::requestNewGameConfirmation();
        }
        else if (optionsInput.pressedFileButton == optionsui::FileButton::Load)
        {
            const auto opened = optionsui::refreshSaveSlots(
                optionsSaveProjection, optionsui::FileDialogMode::Load);
            if (opened)
                optionsProjection.currentScreen = optionsui::Screen::LoadGame;
            else
                engine::playWarningSound();
        }
        else if (optionsInput.pressedFileButton == optionsui::FileButton::Save)
        {
            const auto opened = optionsui::refreshSaveSlots(
                optionsSaveProjection, optionsui::FileDialogMode::Save);
            if (opened)
                optionsProjection.currentScreen = optionsui::Screen::LoadGame;
            else
                engine::playWarningSound();
        }
        else if (optionsInput.pressedFileButton == optionsui::FileButton::Exit)
        {
            optionsProjection.active = false;
            (void)ibar::requestExitConfirmation();
        }
        if (optionsInput.pressedMenuButton == optionsui::MenuButton::Option)
        {
            const auto& displayState = display::stateReadOnly();
            optionsui::loadSupportedOptionValues(optionsProjection,
                displayState.optionTokenVoicesOn, displayState.optionHostCommentsOn,
                displayState.optionMusicOn, displayState.optionMusicTuneIndex,
                displayState.optionTokenAnimationsOn,
                displayState.optionCameraMovementOn,
                displayState.optionLightingOn, displayState.game3DOn,
                displayState.optionFilteringOn);
        }
        if (optionsInput.pressedOptionOkay && optionsProjection.optionSnapshotLoaded)
        {
            const auto value = [&](optionsui::OptionToggle toggle)
            {
                return optionsProjection.optionOn[static_cast<std::size_t>(toggle)];
            };
            display::applyTokenVoicesOption(value(optionsui::OptionToggle::TokenVoices));
            display::applyHostCommentsOption(value(optionsui::OptionToggle::HostComments));
            display::applyMusicOption(value(optionsui::OptionToggle::Music));
            display::applyMusicTune(optionsProjection.musicTuneIndex);
            display::state().optionFilteringOn = value(optionsui::OptionToggle::Filtering);
            display::applyRuntimeOptions(
                value(optionsui::OptionToggle::TokenAnimations),
                value(optionsui::OptionToggle::Camera),
                value(optionsui::OptionToggle::Lighting),
                value(optionsui::OptionToggle::Board3D));
        }
        if (optionsInput.requestedBackdrop)
            display::setBackdrop(*optionsInput.requestedBackdrop);


        if (!customBoardInput.consumed)
            playerselection::processLibraryMessage(message);
        if (playerselection::consumeCustomBoardRequest())
        {
            const auto previous = display::stateReadOnly().desired2DView;
            const char* basePath = SDL_GetBasePath();
            const auto opened = basePath && *basePath
                ? optionsui::openCustomBoardDialog(optionsCustomBoardProjection,
                    std::filesystem::path(basePath), previous)
                : std::expected<void, std::string>(
                    std::unexpected("custom-board executable directory is unavailable"));
            if (opened)
            {
                optionsProjection.previousView = previous;
                optionsProjection.currentScreen = optionsui::Screen::LoadBoard;
                optionsProjection.active = true;
                display::setBackdrop(display::Screen2D::Options);
            }
            else engine::playWarningSound();
        }
        if (playerselection::consumeLoadRequest())
        {
            const auto opened = optionsui::refreshSaveSlots(
                optionsSaveProjection, optionsui::FileDialogMode::Load);
            if (opened)
            {
                optionsProjection.previousView = display::Screen2D::PlayerSelect;
                optionsProjection.currentScreen = optionsui::Screen::LoadGame;
                optionsProjection.active = true;
                display::setBackdrop(display::Screen2D::Options);
            }
            else engine::playWarningSound();
        }
        // Retail input order places UDSTATS after UDPSEL and before UDTRADE.
        // Entering Portfolio through IBar in this same message also initializes
        // the default Player/Turn projection immediately.
        (void)statsui::processInput(
            statsProjection, uiRuleState, display::state().desired2DView, message);
        const auto historyScroll = statsui::historyScrollInput(
            statsProjection, display::state().desired2DView, message);
        if (auto* accounts = engine::statsAccounts()) accounts->scroll(historyScroll);
        (void)statsui::processFutureImmunityInput(
            statsFutureImmunityProjection, uiRuleState,
            statsProjection.screen, display::state().desired2DView, message);
        (void)statsui::processCalculatorInput(
            statsCalculatorProjection, uiRuleState,
            display::state().desired2DView, message);
        if (message.type == uimsg::Type::MouseLeftDown &&
            display::state().desired2DView == display::Screen2D::Portfolio)
        {
            if (const auto square = statsui::propertyActionHit(
                    statsProjection, uiRuleState,
                    static_cast<int>(message.numberA),
                    static_cast<int>(message.numberB)))
                (void)ibar::activateProperty(*square);
        }

        const bool tradePartnerDialogWasVisible = tradeProjection.playerSelectVisible;
        if (const auto partner = tradeui::planPartnerSelection(
                tradeProjection, uiRuleState, display::state().desired2DView,
                message))
        {
            (void)tradeui::selectPartner(tradeProjection, uiRuleState, *partner);
        }
        if (!tradePartnerDialogWasVisible)
        {
            const auto tradeInput = tradeui::processInput(
                tradeProjection, uiRuleState, display::state().desired2DView, message);
            if (tradeInput.proposeMissingOffer)
                engine::playPennybagsVoice(
                    udsound::PennybagsVoice::TradeScreen_ProposeClickedButOneSideHasNotOfferedAnything,
                    udsound::TokenVoiceClipPolicy::ClipOldSoundIfPlaying, false);
            if (tradeInput.requestedBackdrop)
                display::setBackdrop(*tradeInput.requestedBackdrop);
            (void)dispatchTradeBatch(tradeInput.outgoing);
        }
        update();

        // Correspond ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â  ProcessUIMessage() de Main.cpp.
        //
        // ProcessLibraryMessage() sera portÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â© ici progressivement,
        // notamment AdvanceTimeStep(), clavier, souris et sÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â©quenceur.

        if (message.type == uimsg::Type::Quit)
        {
            runtime::state().gameQuitRequested = true;
        }

        // Source originale :
        // if (GameQuitRequested)
        //     return FALSE;

        return !runtime::state().gameQuitRequested;
    }
}




