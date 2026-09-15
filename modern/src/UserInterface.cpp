#include "UserInterface.hpp"
#include "UISound.hpp"
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
#include "UDPennyVoice.hpp"

#include "RuntimeState.hpp"

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

        constexpr std::array<std::uint32_t, rules::SquareCount> ResyncPropertyBits{{
            0, 1u<<0, 0, 1u<<1, 0, 1u<<2, 1u<<3, 0, 1u<<4, 1u<<5,
            0, 1u<<6, 1u<<7, 1u<<8, 1u<<9, 1u<<10, 1u<<11, 0, 1u<<12, 1u<<13,
            0, 1u<<14, 0, 1u<<15, 1u<<16, 1u<<17, 1u<<18, 1u<<19, 1u<<20, 1u<<21,
            0, 1u<<22, 1u<<23, 0, 1u<<24, 1u<<25, 0, 1u<<26, 0, 1u<<27, 0, 0
        }};

        bool applyClientResyncBlob(
            rules::GameState& state,
            const std::vector<std::uint8_t>& data)
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

            (void)readU8(); // resync cause
            (void)readU8(); // authoritative RULE phase
            const std::uint32_t firstMoves = readU32();
            for (rules::PlayerNumber player = 0; player < rules::MaxPlayers; ++player)
                next.players[player].firstMoveMade = (firstMoves & (1u << player)) != 0;
            const auto current = readU8();
            if (current >= rules::MaxPlayers) return false;
            next.currentPlayer = current;
            state = std::move(next);
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
    namespace
    {
        rules::GameState uiRuleState{};
        pieces::PieceMoveIngress pieceMoveIngress;
        pieces::PieceIdleState pieceIdleState;
        dice::Ingress diceIngress;
        std::optional<pieces::PieceIdleTransitionPlan> pendingPieceIdleTransition;
        bool firstNumberOfPlayersNotification = true;
        std::int64_t lastHousingShortageCount = 2;
        std::array<std::uint64_t, rules::MaxPlayers> lastRaiseMoneySoundTick{};
        std::array<std::uint64_t, rules::MaxPlayers> lastBssmBuySoundTick{};
        std::uint64_t lastTradeInitiatorSoundTick{};
        inline constexpr std::uint64_t RaiseMoneyRepeatTicks = 40u * 60u;
        inline constexpr std::uint64_t BssmRepeatTicks = 30u * 60u;
        inline constexpr std::uint64_t TradeInitiatorRepeatTicks = 5u * 60u;

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
            // l'état complet n'est effacé que pour un compteur nul. La
            // première notification non nulle initialise néanmoins les
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

    std::expected<void, std::string> sendAuctionReadyResponses(
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
                "message queue cannot fit auction I_AM_HERE responses");
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
                    "validated auction I_AM_HERE response was rejected");
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
        chat::reset();
        pendingPieceIdleTransition.reset();
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

        (void)chat::processRuleMessage(message);

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
            (void)applyClientResyncBlob(uiRuleState, message.binaryDataA);

        dicePrompt.process(message);
        ibar::processRuleMessage(message, iBarRuleProjection.mode);

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
            uiRuleState.squares[static_cast<std::size_t>(message.numberA)].mortgaged =
                message.numberB != 0;

        if (message.action == actions::Type::NotifySquareHouses &&
            message.numberA >= 0 && message.numberA < rules::SquareCount &&
            message.numberB >= 0 && message.numberB <= 255 &&
            message.numberC >= 0 && message.numberC <= 255)
        {
            auto& square = uiRuleState.squares[static_cast<std::size_t>(message.numberA)];
            const auto previousHouses = square.houses;
            square.houses = static_cast<std::uint8_t>(message.numberB);
            uiRuleState.options.housesPerHotel = static_cast<std::uint8_t>(message.numberC);
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
        const auto auctionUpdate = auctionui::processRuleMessage(
            auctionProjection, uiRuleState, message, display::state().desired2DView);
        if (auctionUpdate.requestedBackdrop)
            display::setBackdrop(*auctionUpdate.requestedBackdrop);
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
            iBarRuleProjection.process(message);
            if (message.action == actions::Type::NotifyTradeAcceptanceDecision)
            {
                const std::uint32_t pendingPlayers = message.numberA > 0
                    ? static_cast<std::uint32_t>(message.numberA)
                    : 0u;
                const auto tradePlayer = ui::localplayers::tradeAcceptanceIBarPlayer(
                    uiRuleState, iBarRuleProjection.tradeBPlayer, pendingPlayers);
                iBarRuleProjection.processTradeAcceptance(message, tradePlayer);
            }
        }

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
        }

        if (message.action == actions::Type::NotifyPleaseRollDice)
        {
            // UDIBar.cpp:3397-3403 selects the 15-tile roll view from the
            // authoritative current player's square before exposing StartTurn.
            if (uiRuleState.currentPlayer < uiRuleState.numberOfPlayers &&
                uiRuleState.currentPlayer < rules::MaxPlayers)
            {
                auto& displayState = display::state();
                displayState.desiredBoardCamera = pieces::selectAppropriateView(
                    pieces::BoardViewSelectionType::RollDice,
                    displayState.desiredBoardCamera,
                    uiRuleState.players[uiRuleState.currentPlayer].currentSquare,
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


        // CheckForAcceptingOurNewPlayer() doit précéder
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
        // ProcessLibraryMessage() original appelle
        // AdvanceTimeStep() à chaque message ArtLib.
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
        ibar::processLibraryMessage(
            message
        );
        const auto optionsInput = optionsui::processInput(
            optionsProjection, display::state().desired2DView, message);
        const bool optionClicked = optionsInput.pressedMenuButton.has_value() ||
            optionsInput.pressedFileButton.has_value() ||
            optionsInput.pressedHelpButton.has_value() ||
            optionsInput.pressedOptionToggle.has_value() ||
            optionsInput.pressedOptionOkay;
        if (optionClicked) engine::playClickSound();
        if (optionsInput.pressedMenuButton == optionsui::MenuButton::Option)
        {
            const auto& displayState = display::stateReadOnly();
            optionsui::loadSupportedOptionValues(optionsProjection,
                displayState.optionTokenVoicesOn, displayState.optionHostCommentsOn,
                displayState.optionMusicOn, displayState.optionMusicTuneIndex,
                displayState.optionTokenAnimationsOn,
                displayState.optionCameraMovementOn,
                displayState.optionLightingOn, displayState.game3DOn);
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
            display::applyRuntimeOptions(
                value(optionsui::OptionToggle::TokenAnimations),
                value(optionsui::OptionToggle::Camera),
                value(optionsui::OptionToggle::Lighting),
                value(optionsui::OptionToggle::Board3D));
        }
        if (optionsInput.requestedBackdrop)
            display::setBackdrop(*optionsInput.requestedBackdrop);


        playerselection::processLibraryMessage(message);
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

        // Correspond à ProcessUIMessage() de Main.cpp.
        //
        // ProcessLibraryMessage() sera porté ici progressivement,
        // notamment AdvanceTimeStep(), clavier, souris et séquenceur.

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




