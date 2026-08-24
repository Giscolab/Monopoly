#include "RuleStartingOrder.hpp"

#include "LegacyTextIds.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"
#include "RuleGameStart.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace monopoly::rules::startingorder
{
    namespace
    {
        using PlayerSet = std::uint32_t;

        constexpr std::uint8_t OffBoardSquare = 41;


        void notifyPlayerOrder(
            const GameState& state)
        {
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


        void sortPlayersByRollHistory(
            GameState& state)
        {
            // SortPlayersByRollHistory() original.

            struct Order
            {
                PlayerNumber player = 0;
                std::uint32_t rollHistory = 0;
            };


            std::array<Order, MaxPlayers>
                newPlayerOrder{};


            std::array<PlayerState, MaxPlayers>
                originalPlayers{};


            for (PlayerNumber playerNo = 0;
                 playerNo < state.numberOfPlayers;
                 ++playerNo)
            {
                newPlayerOrder[playerNo].player =
                    playerNo;

                newPlayerOrder[playerNo].rollHistory =
                    state.players[
                        playerNo
                    ].diceRollHistory;
            }


            // Bubble sort décroissant.
            //
            // Le test est strictement "<", ce qui conserve
            // l'ordre existant en cas d'égalité résiduelle.

            for (
                int playerNo =
                    static_cast<int>(
                        state.numberOfPlayers
                    ) - 1;

                playerNo >= 1;

                --playerNo)
            {
                for (
                    int otherNo = 0;

                    otherNo < playerNo;

                    ++otherNo)
                {
                    if (
                        newPlayerOrder[
                            otherNo
                        ].rollHistory <
                        newPlayerOrder[
                            otherNo + 1
                        ].rollHistory)
                    {
                        std::swap(
                            newPlayerOrder[
                                otherNo
                            ],
                            newPlayerOrder[
                                otherNo + 1
                            ]
                        );
                    }
                }
            }


            for (PlayerNumber playerNo = 0;
                 playerNo < state.numberOfPlayers;
                 ++playerNo)
            {
                originalPlayers[playerNo] =
                    state.players[playerNo];
            }


            for (PlayerNumber playerNo = 0;
                 playerNo < state.numberOfPlayers;
                 ++playerNo)
            {
                state.players[playerNo] =
                    originalPlayers[
                        newPlayerOrder[
                            playerNo
                        ].player
                    ];
            }


            // MESS_AssociatePlayerWithAddress() sera ajouté
            // avec le transport réseau.
            //
            // L'ordre logique des PlayerState est déjà celui
            // du source original.

            notifyPlayerOrder(state);
        }
    }


    void recordDiceRoll(
        GameState& state,
        std::uint8_t dieA,
        std::uint8_t dieB)
    {
        // Branche GF_PICKING_STARTING_ORDER
        // de ActionRollDice().

        if (
            state.numberOfPendingPhases == 0 ||
            phases::current(state).phase !=
                GamePhase::PickingStartingOrder ||
            state.currentPlayer >=
                state.numberOfPlayers)
        {
            return;
        }


        state.dice[0] = dieA;
        state.dice[1] = dieB;


        const std::uint32_t total =
            static_cast<std::uint32_t>(
                dieA + dieB
            );


        std::uint32_t shiftValue =
            state.players[
                state.currentPlayer
            ].diceRollHistory;


        int i = 0;


        // Exactement l'encodage 32 bits original :
        //
        // premier lancer dans le nibble le plus haut,
        // puis les éventuels tie-breaks vers le bas.

        while (
            (shiftValue & 0xFu) == 0 &&
            i < 32)
        {
            i += 4;
            shiftValue >>= 4;
        }


        if (i >= 4)
        {
            state.players[
                state.currentPlayer
            ].diceRollHistory |=
                (
                    total <<
                    (i - 4)
                );
        }


        // Ce joueur a répondu au roll-off courant.

        const std::uint32_t bit =
            1u << state.currentPlayer;


        state.phaseStack[0].fromPlayer =
            static_cast<PlayerNumber>(
                static_cast<std::uint32_t>(
                    state.phaseStack[0].fromPlayer
                ) &
                ~bit
            );


        messaging::sendAction(
            actions::Type::RestartPhase,
            BankPlayer,
            BankPlayer
        );
    }


    void restart(
        GameState& state)
    {
        // AskForStartingOrderDiceRolls() original.

        if (
            state.numberOfPendingPhases == 0 ||
            phases::current(state).phase !=
                GamePhase::PickingStartingOrder)
        {
            return;
        }


        // ----------------------------------------------------
        // Un roll-off est déjà en cours :
        // demander le prochain joueur du set.
        // ----------------------------------------------------

        if (
            state.phaseStack[0].fromPlayer != 0)
        {
            const std::uint32_t waitingSet =
                state.phaseStack[0].fromPlayer;


            for (PlayerNumber playerNo = 0;
                 playerNo < state.numberOfPlayers;
                 ++playerNo)
            {
                if (
                    (
                        waitingSet &
                        (1u << playerNo)
                    ) == 0)
                {
                    continue;
                }


                state.currentPlayer =
                    playerNo;


                messaging::sendAction(
                    actions::Type::NotifyPleaseRollDice,
                    BankPlayer,
                    AllPlayers,
                    state.currentPlayer,
                    OffBoardSquare
                );


                return;
            }
        }


        // ----------------------------------------------------
        // Chercher une égalité.
        // ----------------------------------------------------

        for (PlayerNumber playerNo = 0;
             playerNo < state.numberOfPlayers;
             ++playerNo)
        {
            const std::uint32_t tieDieValue =
                state.players[
                    playerNo
                ].diceRollHistory;


            int numberOfTiedPlayers = 0;

            PlayerSet tiePlayerSet = 0;


            for (PlayerNumber otherNo = 0;
                 otherNo < state.numberOfPlayers;
                 ++otherNo)
            {
                const std::uint32_t dieValue =
                    state.players[
                        otherNo
                    ].diceRollHistory;


                if (dieValue ==
                    tieDieValue)
                {
                    ++numberOfTiedPlayers;

                    tiePlayerSet |=
                        1u << otherNo;
                }
            }


            // Si le nibble bas est encore zéro,
            // il reste de la place pour un nouveau tie-break.

            if (
                numberOfTiedPlayers > 1 &&
                (tieDieValue & 0xFu) == 0)
            {
                state.phaseStack[0].fromPlayer =
                    static_cast<PlayerNumber>(
                        tiePlayerSet
                    );


                // Ordre exact de Rule.cpp :
                // RESTART puis message tie-rolloff.

                messaging::sendAction(
                    actions::Type::RestartPhase,
                    BankPlayer,
                    BankPlayer
                );


                messaging::sendAction(
                    actions::Type::NotifyErrorMessage,
                    BankPlayer,
                    AllPlayers,
                    legacy_text::ErrorTieRolloff,
                    numberOfTiedPlayers - 1,
                    playerNo,
                    numberOfTiedPlayers
                );


                return;
            }
        }


        // ----------------------------------------------------
        // Ordre définitif.
        // ----------------------------------------------------

        sortPlayersByRollHistory(state);


        // Puis StartGameInitialisation().
        gamestart::startGameAfterOrdering(
            state
        );
    }
}
