#include "RuleCoreActions.hpp"

#include "BoardRules.hpp"
#include "LegacyTextIds.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"
#include "RuleBuildings.hpp"
#include "RuleEconomy.hpp"
#include "RuleRandom.hpp"
#include "RuleOptions.hpp"
#include "RuleSave.hpp"
#include "RuleSynchronization.hpp"
#include "RuleTrade.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace monopoly::rules::coreactions
{
    namespace
    {
        void resetTransientSubsystems()
        {
            economy::resetTransientState();
            buildings::resetTransientState();
            trade::resetTransientState();
            save::resetTransientState();
            sync::resetWaitGate();
        }


        void defaultOptions(
            GameOptions& gameOptions)
        {
            options::setDefaults(
                gameOptions
            );
        }


        void resetState(
            GameState& state,
            bool preservePlayers,
            const GameOptions& savedOptions,
            const std::array<PlayerState, MaxPlayers>&
                savedPlayers,
            PlayerNumber savedCount)
        {
            state = GameState{};

            resetTransientSubsystems();


            state.currentPlayer =
                NobodyPlayer;


            for (SquareState& square :
                 state.squares)
            {
                square.owner =
                    NobodyPlayer;

                square.offeredInTradeTo =
                    NobodyPlayer;

                square.gameEarnings = 0;
            }


            for (CountHitRecord& hit :
                 state.countHits)
            {
                hit.toPlayer =
                    NobodyPlayer;
            }


            if (preservePlayers)
            {
                state.options =
                    savedOptions;

                state.numberOfPlayers =
                    savedCount;


                for (PlayerNumber player = 0;
                     player < savedCount;
                     ++player)
                {
                    // ActionNewGame() restaure uniquement
                    // identité + type de joueur.

                    state.players[player].name =
                        savedPlayers[player].name;

                    state.players[player].token =
                        savedPlayers[player].token;

                    state.players[player].colour =
                        savedPlayers[player].colour;

                    state.players[player]
                        .aiPlayerLevel =
                        savedPlayers[player]
                            .aiPlayerLevel;
                }
            }
            else
            {
                defaultOptions(
                    state.options
                );
            }


            phases::push(
                state,
                GamePhase::AddingNewPlayers,
                0,
                0,
                0
            );


            if (preservePlayers)
            {
                // Start right away.

                messaging::sendAction(
                    actions::Type::StartGame,
                    BankPlayer,
                    BankPlayer
                );
            }
            else
            {
                messaging::sendAction(
                    actions::Type::NotifyErrorMessage,
                    BankPlayer,
                    AllPlayers,
                    legacy_text::AddingPlayers,
                    MaxPlayers -
                        state.numberOfPlayers,
                    0,
                    0
                );


                messaging::sendAction(
                    actions::Type::RestartPhase,
                    BankPlayer,
                    BankPlayer
                );
            }
        }


        void wrongPlayer(
            const GameState& state,
            const actions::Message& message)
        {
            messaging::sendAction(
                actions::Type::NotifyActionCompleted,
                BankPlayer,
                AllPlayers,
                static_cast<std::int64_t>(
                    message.action
                ),
                0,
                message.fromPlayer,
                0
            );


            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorWrongPlayer,
                static_cast<std::int64_t>(
                    message.action
                ),
                message.fromPlayer,
                state.numberOfPendingPhases > 0
                    ? static_cast<std::int64_t>(
                        phases::current(state).phase
                    )
                    : 0
            );
        }


        void wrongPhase(
            const GameState& state,
            const actions::Message& message)
        {
            messaging::sendAction(
                actions::Type::NotifyActionCompleted,
                BankPlayer,
                AllPlayers,
                static_cast<std::int64_t>(
                    message.action
                ),
                0,
                message.fromPlayer,
                0
            );


            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorWrongPhase,
                static_cast<std::int64_t>(
                    message.action
                ),
                message.fromPlayer,
                state.numberOfPendingPhases > 0
                    ? static_cast<std::int64_t>(
                        phases::current(state).phase
                    )
                    : 0
            );
        }


        void cheatingMessage(
            PlayerNumber player)
        {
            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorPlayerCheating,
                0,
                player,
                0
            );
        }
    }


    void resetFreshUnconditional(
        GameState& state)
    {
        // RULE_InitializeSystem() :
        //
        // srand(GetTickCount());
        // InitialisePredefinedData(fake housesPerHotel=5);
        // ActionNewGame(NULL).

        random::initialize();


        GameOptions fake{};

        fake.housesPerHotel = 5;


        board::initializeForOptions(
            fake
        );


        const std::array<
            PlayerState,
            MaxPlayers
        > emptyPlayers{};


        GameOptions ignored{};


        resetState(
            state,
            false,
            ignored,
            emptyPlayers,
            0
        );
    }


    void actionNewGame(
        GameState& state,
        const actions::Message& message)
    {
        // ActionNewGame() original.

        if (
            !state.options.cheatingAllowed &&
            message.fromPlayer >=
                state.numberOfPlayers)
        {
            wrongPlayer(
                state,
                message
            );

            return;
        }


        bool preservePlayers = false;


        if (
            state.numberOfPendingPhases > 0 &&
            phases::current(state).phase ==
                GamePhase::GameFinished &&
            message.numberA != 0)
        {
            preservePlayers = true;
        }


        const PlayerNumber savedCount =
            preservePlayers
                ? state.numberOfPlayers
                : 0;


        GameOptions savedOptions{};

        std::array<PlayerState, MaxPlayers>
            savedPlayers{};


        if (preservePlayers)
        {
            savedOptions =
                state.options;


            for (PlayerNumber player = 0;
                 player < savedCount;
                 ++player)
            {
                savedPlayers[player] =
                    state.players[player];
            }
        }


        // NOTIFY_ACTION_COMPLETED :
        //
        // A = ACTION_NEW_GAME
        // B = TRUE
        // C = requesting player
        // D = number of retained players

        actions::Message completed{};

        completed.action =
            actions::Type::NotifyActionCompleted;

        completed.fromPlayer =
            BankPlayer;

        completed.toPlayer =
            AllPlayers;

        completed.numberA =
            static_cast<std::int64_t>(
                actions::Type::NewGame
            );

        completed.numberB = 1;

        completed.numberC =
            message.fromPlayer;

        completed.numberD =
            savedCount;


        messaging::sendAction(
            completed
        );


        resetState(
            state,
            preservePlayers,
            savedOptions,
            savedPlayers,
            savedCount
        );
    }


    void actionRandomSeed(
        const actions::Message& message)
    {
        // ACTION_RANDOM_SEED :
        // srand(numberA).

        random::seed(
            static_cast<std::uint32_t>(
                message.numberA
            )
        );
    }


    void actionCheatCash(
        GameState& state,
        const actions::Message& message)
    {
        // ActionCheatCash() original.

        const PlayerNumber player =
            static_cast<PlayerNumber>(
                message.numberA
            );


        const std::int64_t newCash =
            message.numberB;


        if (
            !state.options.cheatingAllowed ||
            player >= MaxPlayers ||
            newCash < 0)
        {
            return;
        }


        const std::int64_t oldCash =
            state.players[player].cash;


        state.players[player].cash =
            newCash;


        messaging::sendAction(
            actions::Type::NotifyCashAmount,
            BankPlayer,
            AllPlayers,
            player,
            newCash - oldCash,
            newCash
        );


        cheatingMessage(
            message.fromPlayer
        );
    }


    void actionCheatOwner(
        GameState& state,
        const actions::Message& message)
    {
        // ActionCheatOwner() original.

        PlayerNumber player =
            static_cast<PlayerNumber>(
                message.numberA
            );


        const std::int64_t rawSquare =
            message.numberB;


        if (
            !state.options.cheatingAllowed ||
            player >= MaxPlayers ||
            rawSquare < 0 ||
            rawSquare >=
                static_cast<std::int64_t>(
                    SquareCount
                ))
        {
            return;
        }


        const auto squareType =
            static_cast<board::SquareType>(
                rawSquare
            );


        if (!board::isOwnable(squareType))
        {
            return;
        }


        SquareState& square =
            state.squares[
                static_cast<std::size_t>(
                    rawSquare
                )
            ];


        if (square.owner == player)
        {
            player =
                NobodyPlayer;
        }


        messaging::sendAction(
            actions::Type::NotifySquareOwnership,
            BankPlayer,
            AllPlayers,
            rawSquare,
            player,
            square.owner,
            0
        );


        square.owner =
            player;

        square.offeredInTradeTo =
            NobodyPlayer;


        cheatingMessage(
            message.fromPlayer
        );
    }


    void actionKillAuctionCheat(
        GameState& state,
        const actions::Message& message)
    {
        // ActionKillAuctionCheat() original.

        if (
            state.numberOfPendingPhases == 0 ||
            phases::current(state).phase !=
                GamePhase::Auction)
        {
            wrongPhase(
                state,
                message
            );

            return;
        }


        if (!state.options.cheatingAllowed)
        {
            return;
        }


        state.auction.tickCount =
            static_cast<std::uint8_t>(
                state.options
                    .auctionGoingTimeDelay
            );


        state.auction.goingCount = 2;


        cheatingMessage(
            message.fromPlayer
        );
    }


    void actionEchoChat(
        const actions::Message& message)
    {
        // ActionEchoChat().
        //
        // Le blob peut être vide.

        actions::Message relay{};


        relay.action =
            message.action ==
                actions::Type::VoiceChat
                ? actions::Type::NotifyVoiceChat
                : actions::Type::NotifyTextChat;


        relay.fromPlayer =
            BankPlayer;


        relay.toPlayer =
            static_cast<PlayerNumber>(
                message.numberA
            );


        relay.numberA =
            message.numberA;


        relay.numberB =
            message.fromPlayer;


        relay.numberC =
            message.numberC;


        // NS_LOCAL dans le runtime actuel.
        relay.numberD = 0;


        relay.binaryDataA =
            message.binaryDataA;


        relay.stringA =
            message.stringA;


        messaging::sendAction(
            relay
        );
    }


    void actionUpdateTradeInfo(
        const actions::Message& message)
    {
        // ActionUpdateTradeInfo() original :
        // rebroadcast intégral.

        actions::Message relay =
            message;


        relay.action =
            actions::Type::NotifyUpdateTradeInfo;


        relay.fromPlayer =
            BankPlayer;


        relay.toPlayer =
            AllPlayers;


        messaging::sendAction(
            relay
        );
    }


    void actionStarWarsAnimationInfo(
        const actions::Message& message)
    {
        // ActionStarWarsAnimationInfo() original.

        actions::Message relay =
            message;


        relay.action =
            actions::Type::
                NotifyStarWarsAnimationInfo;


        relay.fromPlayer =
            BankPlayer;


        relay.toPlayer =
            AllPlayers;


        messaging::sendAction(
            relay
        );
    }
}

