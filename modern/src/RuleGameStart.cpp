#include "RuleGameStart.hpp"

#include "RuleInitialDeal.hpp"
#include "BoardRules.hpp"
#include "CardDecks.hpp"
#include "LegacyTextIds.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"
#include "RuleRandom.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace monopoly::rules::gamestart
{
    namespace
    {
        void notifyActionCompleted(
            const actions::Message& action,
            bool success)
        {
            messaging::sendAction(
                actions::Type::NotifyActionCompleted,
                BankPlayer,
                AllPlayers,
                static_cast<std::int64_t>(
                    action.action
                ),
                success ? 1 : 0,
                action.fromPlayer,
                0
            );
        }


        void wrongPhase(
            GameState& state,
            const actions::Message& action)
        {
            notifyActionCompleted(
                action,
                false
            );

            if (action.fromPlayer == BankPlayer)
            {
                return;
            }

            const PlayerNumber destination =
                action.fromPlayer < MaxPlayers
                    ? action.fromPlayer
                    : AllPlayers;

            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                destination,
                legacy_text::ErrorWrongPhase,
                static_cast<std::int64_t>(
                    action.action
                ),
                action.fromPlayer,
                static_cast<std::int64_t>(
                    phases::current(state).phase
                )
            );

            messaging::sendAction(
                actions::Type::RestartPhase,
                BankPlayer,
                BankPlayer
            );
        }


        void wrongPlayer(
            GameState& state,
            const actions::Message& action)
        {
            notifyActionCompleted(
                action,
                false
            );

            if (action.fromPlayer == BankPlayer)
            {
                return;
            }

            const PlayerNumber destination =
                action.fromPlayer < MaxPlayers
                    ? action.fromPlayer
                    : AllPlayers;

            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                destination,
                legacy_text::ErrorWrongPlayer,
                static_cast<std::int64_t>(
                    action.action
                ),
                action.fromPlayer,
                static_cast<std::int64_t>(
                    phases::current(state).phase
                )
            );

            messaging::sendAction(
                actions::Type::RestartPhase,
                BankPlayer,
                BankPlayer
            );
        }


        void clearAcceptConfiguration(
            GameState& state)
        {
            // RULE_PLAYERSVOTEONRULES == 0 dans le source.
            //
            // Tous sont pré-approuvés, puis le premier joueur
            // HUMAIN LOCAL (host) doit confirmer.

            for (PlayerState& player : state.players)
            {
                player.acceptedConfiguration = true;
            }

            // Notre runtime actuel est local uniquement.
            // Le premier humain correspond donc au premier slot
            // local non-AI recherché par le source.

            for (PlayerNumber playerNo = 0;
                 playerNo < state.numberOfPlayers;
                 ++playerNo)
            {
                if (state.players[playerNo]
                        .aiPlayerLevel == 0)
                {
                    state.players[playerNo]
                        .acceptedConfiguration = false;

                    break;
                }
            }
        }


        void randomizePlayerOrder(
            GameState& state)
        {
            auto& rng =
                random::generator();

            for (PlayerNumber playerNo = 0;
                 playerNo < state.numberOfPlayers;
                 ++playerNo)
            {
                state.players[playerNo]
                    .diceRollHistory =
                    rng();
            }


            // SortPlayersByRollHistory() original :
            // bubble sort décroissant.

            for (int playerNo =
                    static_cast<int>(
                        state.numberOfPlayers
                    ) - 1;
                 playerNo >= 1;
                 --playerNo)
            {
                for (int otherNo = 0;
                     otherNo < playerNo;
                     ++otherNo)
                {
                    if (state.players[otherNo]
                            .diceRollHistory <
                        state.players[otherNo + 1]
                            .diceRollHistory)
                    {
                        std::swap(
                            state.players[otherNo],
                            state.players[otherNo + 1]
                        );
                    }
                }
            }


            // L'original rediffuse les nouveaux numéros.

            for (PlayerNumber playerNo = 0;
                 playerNo < state.numberOfPlayers;
                 ++playerNo)
            {
                const PlayerState& player =
                    state.players[playerNo];

                messaging::sendAction(
                    actions::Type::NotifyNamePlayer,
                    BankPlayer,
                    AllPlayers,
                    playerNo,
                    player.token,
                    player.colour,
                    player.aiPlayerLevel,
                    player.name
                );
            }
        }


        void startGameInitialisation(
            GameState& state)
        {
            // ------------------------------------------------
            // InitialisePredefinedData(GameOptions)
            // ------------------------------------------------

            board::initializeForOptions(
                state.options
            );


            // ------------------------------------------------
            // InitialiseDecks()
            // ------------------------------------------------

            cards::initializeDecks(
                state,
                random::generator()
            );


            // ------------------------------------------------
            // Joueurs :
            // cash initial + GO + firstMoveMade FALSE.
            // ------------------------------------------------

            for (PlayerNumber playerNo = 0;
                 playerNo < state.numberOfPlayers;
                 ++playerNo)
            {
                PlayerState& player =
                    state.players[playerNo];

                player.cash =
                    state.options.initialCash;

                player.currentSquare = 0; // SQ_GO

                player.firstMoveMade = false;
            }


            // ------------------------------------------------
            // Reset des 42 cases.
            // ------------------------------------------------

            for (SquareState& square :
                 state.squares)
            {
                square.owner =
                    NobodyPlayer;

                square.offeredInTradeTo =
                    NobodyPlayer;

                square.houses = 0;
                square.mortgaged = false;
                square.gameEarnings = 0;
            }


            state.currentPlayer = 0;

            state.gameDurationInSeconds = 0;

            state.freeParkingJackpotAmount = 0;


            // MESS_UpdateLobbyGameStarted() et
            // SendClientResyncGameState() sont des fonctions
            // de transport réseau.
            //
            // Notre MESS actuel est volontairement local :
            // on ne fabrique pas de faux transport ici.


            messaging::sendAction(
                actions::Type::NotifyGameStarting,
                BankPlayer,
                AllPlayers
            );

            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorGameStarting,
                0,
                state.currentPlayer,
                0
            );


            // SwitchPhase(GF_WAIT_START_TURN)

            phases::switchTo(
                state,
                GamePhase::WaitStartTurn,
                0,
                0,
                0
            );

        // DealNProperties() vient exactement ici dans
        // StartGameInitialisation() original.
        initialdeal::dealNProperties(
            state
        );

// DealNProperties()
            //
            // L'option originale par défaut est 0.
            // Le chemin non-zéro dépend de TransferProperty(),
            // StackDebt() et GF_TRANSFER_ESCROW_PROPERTY.
            //
            // On ne falsifie donc pas cette branche :
            // elle sera portée avec le moteur dette/escrow.

            if (state.options.dealNPropertiesAtStartup != 0)
            {
                // Pas encore exécuté tant que les phases
                // dette/escrow ne sont pas portées.
            }


            // AddMoneyToFreeParkingPot(seed)

            state.freeParkingJackpotAmount +=
                state.options.freeParkingSeed;

            if (state.options.freeParkingPot)
            {
                messaging::sendAction(
                    actions::Type::NotifyFreeParkingPot,
                    BankPlayer,
                    AllPlayers,
                    state.freeParkingJackpotAmount,
                    state.options.freeParkingSeed
                );
            }


            messaging::sendAction(
                actions::Type::RestartPhase,
                BankPlayer,
                BankPlayer
            );
        }
    }


    void actionStartGame(
        GameState& state,
        const actions::Message& message)
    {
        // ActionStartGame() original.

        if (phases::current(state).phase !=
            GamePhase::AddingNewPlayers)
        {
            wrongPhase(state, message);
            return;
        }


        if (!state.options.cheatingAllowed &&
            message.fromPlayer >=
                state.numberOfPlayers &&
            message.fromPlayer != BankPlayer)
        {
            wrongPlayer(state, message);
            return;
        }


        if (state.numberOfPlayers < 2)
        {
            notifyActionCompleted(
                message,
                false
            );

            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorNeedTwoPlayers
            );

            messaging::sendAction(
                actions::Type::RestartPhase,
                BankPlayer,
                BankPlayer
            );

            return;
        }


        // RULE_NO_SPECTATORS_ALLOWED == 0 :
        // aucun blocage spectateur.


        notifyActionCompleted(
            message,
            true
        );


        state.configurationProposer =
            NobodyPlayer;


        phases::switchTo(
            state,
            GamePhase::Configuration,
            0,
            0,
            0
        );


        clearAcceptConfiguration(state);


        messaging::sendAction(
            actions::Type::RestartPhase,
            BankPlayer,
            BankPlayer
        );
    }


    void actionAcceptConfiguration(
        GameState& state,
        const actions::Message& message)
    {
        // ActionAcceptConfiguration() original.

        if (phases::current(state).phase !=
            GamePhase::Configuration)
        {
            wrongPhase(state, message);
            return;
        }

        if (message.fromPlayer >=
            state.numberOfPlayers)
        {
            wrongPlayer(state, message);
            return;
        }


        notifyActionCompleted(
            message,
            true
        );


        // RULE_ConvertFileToGameOptions() n'est pas encore
        // porté.
        //
        // L'original initialise OptionsReceived avec les
        // options courantes AVANT de tenter le décodage.
        //
        // Donc une action sans blob signifie bien :
        // accepter la configuration actuelle.


        // Client version < 1 :
        // futures / immunités indisponibles.

        if (message.numberC < 1)
        {
            state.options.futureRentTradingAllowed =
                false;

            state.options.immunitiesTradingAllowed =
                false;
        }


        // NumberD == 0 : acceptation finale.

        if (message.numberD == 0 &&
            !state.players[
                message.fromPlayer
            ].acceptedConfiguration)
        {
            state.players[
                message.fromPlayer
            ].acceptedConfiguration = true;

            messaging::sendAction(
                actions::Type::RestartPhase,
                BankPlayer,
                BankPlayer
            );
        }
    }


    void startGameAfterOrdering(
        GameState& state)
    {
        // SortPlayersByRollHistory() appelle ensuite
        // directement StartGameInitialisation().

        startGameInitialisation(state);
    }


    void restartConfiguration(
        GameState& state)
    {
        std::uint32_t playerSet = 0;

        for (PlayerNumber playerNo = 0;
             playerNo < state.numberOfPlayers;
             ++playerNo)
        {
            if (!state.players[playerNo]
                    .acceptedConfiguration)
            {
                playerSet |=
                    (1u << playerNo);
            }
        }


        // NOTIFY_PROPOSED_CONFIGURATION.
        //
        // numberA : proposer
        // numberB : joueurs n'ayant pas accepté
        // numberC : version host = 1
        //
        // Le blob RIFF des GameOptions sera ajouté avec
        // RULE_ConvertFileToGameOptions.

        messaging::sendAction(
            actions::Type::NotifyProposedConfiguration,
            BankPlayer,
            AllPlayers,
            state.configurationProposer,
            playerSet,
            1
        );


        if (playerSet != 0)
        {
            return;
        }


        if (state.options
                .rollDiceToDecideStartingOrder)
        {
            for (PlayerNumber playerNo = 0;
                 playerNo < state.numberOfPlayers;
                 ++playerNo)
            {
                state.players[playerNo]
                    .diceRollHistory = 0;
            }

            phases::switchTo(
                state,
                GamePhase::PickingStartingOrder,
                0,
                0,
                0
            );

            messaging::sendAction(
                actions::Type::RestartPhase,
                BankPlayer,
                BankPlayer
            );

            return;
        }


        // Chemin standard du jeu original :
        //
        // RandomizePlayerOrder();
        // StartGameInitialisation();

        randomizePlayerOrder(state);

        startGameInitialisation(state);
    }
}


