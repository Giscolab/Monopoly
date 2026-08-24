#include "RuleLifecycle.hpp"

#include "BoardRules.hpp"
#include "LegacyTextIds.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <string>
#include <string_view>

namespace monopoly::rules::lifecycle
{
    namespace
    {
        constexpr std::uint8_t OffBoardSquare = 41;

        constexpr std::uint8_t MaxAILevels = 3;


        int bankruptPlayerCount(
            const GameState& state)
        {
            int count = 0;

            for (PlayerNumber player = 0;
                 player < state.numberOfPlayers;
                 ++player)
            {
                if (
                    state.players[player]
                        .currentSquare >=
                    OffBoardSquare)
                {
                    ++count;
                }
            }

            return count;
        }


        bool equalsIgnoreCase(
            std::wstring_view lhs,
            std::wstring_view rhs)
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }

            for (std::size_t i = 0;
                 i < lhs.size();
                 ++i)
            {
                if (
                    std::towlower(lhs[i]) !=
                    std::towlower(rhs[i]))
                {
                    return false;
                }
            }

            return true;
        }


        void actionCompleted(
            const actions::Message& message,
            bool success)
        {
            messaging::sendAction(
                actions::Type::NotifyActionCompleted,
                BankPlayer,
                AllPlayers,
                static_cast<std::int64_t>(
                    message.action
                ),
                success ? 1 : 0,
                message.fromPlayer,
                0
            );
        }


        void wrongPlayer(
            const GameState& state,
            const actions::Message& message)
        {
            actionCompleted(
                message,
                false
            );

            if (message.fromPlayer ==
                BankPlayer)
            {
                return;
            }

            const PlayerNumber destination =
                message.fromPlayer < MaxPlayers
                    ? message.fromPlayer
                    : AllPlayers;

            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                destination,
                legacy_text::ErrorWrongPlayer,
                static_cast<std::int64_t>(
                    message.action
                ),
                message.fromPlayer,
                static_cast<std::int64_t>(
                    phases::current(state).phase
                )
            );
        }


        void wrongPhase(
            const GameState& state,
            const actions::Message& message)
        {
            actionCompleted(
                message,
                false
            );

            if (message.fromPlayer ==
                BankPlayer)
            {
                return;
            }

            const PlayerNumber destination =
                message.fromPlayer < MaxPlayers
                    ? message.fromPlayer
                    : AllPlayers;

            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                destination,
                legacy_text::ErrorWrongPhase,
                static_cast<std::int64_t>(
                    message.action
                ),
                message.fromPlayer,
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


        std::int64_t playerWealth(
            const GameState& state,
            PlayerNumber player)
        {
            // GameOverDeclareWinner() original :
            //
            // cash
            // + purchase price si non hypothéqué
            // + mortgage value si hypothéqué
            // + full construction cost des bâtiments.

            std::int64_t wealth =
                state.players[player].cash;


            for (std::size_t squareNo = 0;
                 squareNo < SquareCount;
                 ++squareNo)
            {
                const SquareState& square =
                    state.squares[squareNo];

                if (square.owner != player)
                {
                    continue;
                }


                const board::SquareDefinition& predefined =
                    board::definition(
                        static_cast<board::SquareType>(
                            squareNo
                        )
                    );


                wealth +=
                    square.mortgaged
                        ? predefined.mortgageCost
                        : predefined.purchaseCost;


                wealth +=
                    static_cast<std::int64_t>(
                        square.houses
                    ) *
                    predefined.housePurchaseCost;
            }


            return wealth;
        }
    }


    bool onlyOneSurvivor(
        const GameState& state)
    {
        const int bankrupt =
            bankruptPlayerCount(state);

        return
            state.numberOfPlayers > 0 &&
            bankrupt >=
                static_cast<int>(
                    state.numberOfPlayers
                ) - 1;
    }


    bool shouldEndGame(
        const GameState& state)
    {
        const int bankrupt =
            bankruptPlayerCount(state);


        // ActionEndTurn() original.

        if (
            state.options.stopAtNthBankruptcy != 0 &&
            bankrupt >=
                state.options.stopAtNthBankruptcy)
        {
            return true;
        }


        if (
            state.options.gameOverTimeLimit != 0 &&
            state.gameDurationInSeconds >=
                static_cast<std::uint64_t>(
                    state.options.gameOverTimeLimit
                ))
        {
            return true;
        }


        if (
            state.numberOfPlayers > 0 &&
            bankrupt >=
                static_cast<int>(
                    state.numberOfPlayers
                ) - 1)
        {
            return true;
        }


        return false;
    }


    void declareWinner(
        GameState& state)
    {
        // ====================================================
        // GameOverDeclareWinner() original.
        // ====================================================

        state.currentPlayer =
            NobodyPlayer;


        int numberOfBankruptPlayers = 0;

        std::int64_t highestWealth = -1;

        PlayerNumber richestPlayer =
            NobodyPlayer;


        for (PlayerNumber player = 0;
             player < state.numberOfPlayers;
             ++player)
        {
            if (
                state.players[player]
                    .currentSquare >=
                OffBoardSquare)
            {
                ++numberOfBankruptPlayers;
                continue;
            }


            const std::int64_t wealth =
                playerWealth(
                    state,
                    player
                );


            // Strictement > dans Rule.cpp.
            // En cas d'égalité, le premier reste gagnant.
            if (wealth > highestWealth)
            {
                highestWealth =
                    wealth;

                richestPlayer =
                    player;
            }
        }


        // PushPhase(
        //   GF_GAME_FINISHED,
        //   RichestPlayer,
        //   RULE_NOBODY_PLAYER,
        //   HighestWealth
        // );

        phases::push(
            state,
            GamePhase::GameFinished,
            richestPlayer,
            NobodyPlayer,
            highestWealth
        );


        messaging::sendAction(
            actions::Type::RestartPhase,
            BankPlayer,
            BankPlayer
        );


        // ----------------------------------------------------
        // Retirer tous les survivants du plateau.
        // ----------------------------------------------------

        for (PlayerNumber player = 0;
             player < state.numberOfPlayers;
             ++player)
        {
            if (
                state.players[player]
                    .currentSquare >=
                OffBoardSquare)
            {
                continue;
            }


            messaging::sendAction(
                actions::Type::NotifyJumpToSquare,
                BankPlayer,
                AllPlayers,
                OffBoardSquare,
                state.players[player]
                    .currentSquare,
                player,
                0
            );


            state.players[player]
                .currentSquare =
                OffBoardSquare;
        }


        // ----------------------------------------------------
        // Diagnostic exact de la raison.
        // ----------------------------------------------------

        if (
            state.options.stopAtNthBankruptcy != 0 &&
            numberOfBankruptPlayers >=
                state.options.stopAtNthBankruptcy)
        {
            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorGameWonShort,
                richestPlayer,
                highestWealth,
                numberOfBankruptPlayers
            );

            return;
        }


        if (
            state.options.gameOverTimeLimit != 0 &&
            state.gameDurationInSeconds >=
                static_cast<std::uint64_t>(
                    state.options.gameOverTimeLimit
                ))
        {
            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorGameWonTime,
                richestPlayer,
                highestWealth,
                state.options.gameOverTimeLimit
            );

            return;
        }


        messaging::sendAction(
            actions::Type::NotifyErrorMessage,
            BankPlayer,
            AllPlayers,
            legacy_text::ErrorGameWonNormal,
            richestPlayer,
            highestWealth,
            0
        );


        // SendGameStatusToLobby() et
        // MESS_UpdateLobbyGameFinished()
        // appartiennent au transport réseau/lobby et ne sont
        // pas simulés dans le runtime local.
    }


    void actionPauseGame(
        GameState& state,
        const actions::Message& message)
    {
        // ====================================================
        // ActionPauseGame() original.
        // ====================================================

        const PlayerNumber player =
            message.fromPlayer;


        if (player >=
            state.numberOfPlayers)
        {
            wrongPlayer(
                state,
                message
            );

            return;
        }


        // Le source envoie ACTION_COMPLETED(TRUE)
        // avant même de vérifier MOVING_TOKEN / WAIT_END_TURN.
        actionCompleted(
            message,
            true
        );


        if (message.numberA != 0)
        {
            // Pause.

            if (
                phases::current(state).phase !=
                GamePhase::Paused)
            {
                // Ces deux phases ne sont pas restartables
                // proprement après une pause.

                if (
                    phases::current(state).phase ==
                        GamePhase::MovingToken ||
                    phases::current(state).phase ==
                        GamePhase::WaitEndTurn)
                {
                    // Oui : l'original peut produire ici
                    // ACTION_COMPLETED(TRUE), puis FALSE via
                    // ErrorWrongPhase().
                    wrongPhase(
                        state,
                        message
                    );
                }
                else
                {
                    messaging::sendAction(
                        actions::Type::NotifyErrorMessage,
                        BankPlayer,
                        AllPlayers,
                        legacy_text::GamePaused,
                        player,
                        0,
                        0
                    );


                    phases::push(
                        state,
                        GamePhase::Paused,
                        0,
                        0,
                        0
                    );
                }
            }
        }
        else
        {
            // Reprise.

            if (
                phases::current(state).phase ==
                GamePhase::Paused)
            {
                messaging::sendAction(
                    actions::Type::NotifyErrorMessage,
                    BankPlayer,
                    AllPlayers,
                    legacy_text::GameUnpaused,
                    player,
                    0,
                    0
                );


                phases::pop(state);
            }
        }


        // ActionPauseGame() finit toujours par un restart.
        messaging::sendAction(
            actions::Type::RestartPhase,
            BankPlayer,
            BankPlayer
        );
    }


    void actionDisconnectedPlayer(
        GameState& state,
        const actions::Message& message)
    {
        // ====================================================
        // ActionDisconnectedPlayer() original.
        //
        // Le runtime moderne n'a pas encore de NetworkAddress.
        // Toutes les actions MESS actuelles sont locales :
        // cela correspond au chemin NS_LOCAL du source.
        // ====================================================

        if (
            message.numberA < 0 ||
            message.numberA >=
                state.numberOfPlayers)
        {
            wrongPlayer(
                state,
                message
            );

            return;
        }


        const PlayerNumber player =
            static_cast<PlayerNumber>(
                message.numberA
            );


        actionCompleted(
            message,
            true
        );


        messaging::sendAction(
            actions::Type::NotifyErrorMessage,
            BankPlayer,
            AllPlayers,
            legacy_text::ErrorPlayerDisconnected,
            0,
            player,
            0
        );


        // ----------------------------------------------------
        // IA de remplacement.
        //
        // Rule.cpp :
        // level fourni si 1..AI_MAX_LEVELS,
        // sinon AI_MAX_LEVELS = 3.
        // ----------------------------------------------------

        std::int64_t aiLevel =
            message.numberB;


        if (
            aiLevel < 1 ||
            aiLevel > MaxAILevels)
        {
            aiLevel =
                MaxAILevels;
        }


        state.players[player]
            .aiPlayerLevel =
            static_cast<std::uint8_t>(
                aiLevel
            );


        // ----------------------------------------------------
        // Garantir un nom unique.
        //
        // Le source conserve le nom existant et ajoute "?"
        // tant qu'il y a collision.
        // ----------------------------------------------------

        std::wstring& name =
            state.players[player].name;


        while (true)
        {
            bool uniqueNameFound = true;


            for (PlayerNumber other = 0;
                 other <
                    state.numberOfPlayers;
                 ++other)
            {
                if (other == player)
                {
                    continue;
                }


                if (
                    equalsIgnoreCase(
                        state.players[other].name,
                        name
                    ))
                {
                    uniqueNameFound = false;
                    break;
                }
            }


            if (uniqueNameFound)
            {
                break;
            }


            if (
                name.size() >=
                MaxPlayerNameLength)
            {
                name = L"?";
            }
            else
            {
                name += L"?";
            }
        }


        // ----------------------------------------------------
        // NOTIFY_ADD_LOCAL_PLAYER
        // ----------------------------------------------------

        messaging::sendAction(
            actions::Type::NotifyAddLocalPlayer,
            BankPlayer,
            AllPlayers,
            player,
            0,
            0,
            0,
            name
        );


        // ----------------------------------------------------
        // NOTIFY_NAME_PLAYER
        // ----------------------------------------------------

        messaging::sendAction(
            actions::Type::NotifyNamePlayer,
            BankPlayer,
            AllPlayers,
            player,
            state.players[player].token,
            state.players[player].colour,
            state.players[player]
                .aiPlayerLevel,
            name
        );


        // L'IA qui vient de prendre le slot doit savoir
        // immédiatement où le jeu en est.
        messaging::sendAction(
            actions::Type::RestartPhase,
            BankPlayer,
            BankPlayer
        );
    }


    void recordPlayerActivity(
        GameState& state,
        PlayerNumber player)
    {
        // RULE_ProcessRules() original :
        //
        // if (PlayerNo < RULE_MAX_PLAYERS)
        //
        // et non NumberOfPlayers.

        if (player >= MaxPlayers)
        {
            return;
        }


        state.players[player]
            .timeOfLastActivity =
            static_cast<std::uint32_t>(
                state.gameDurationInSeconds
            );


        state.players[player]
            .inactivityCount = 0;
    }


    void onIdleTick(
        GameState& state)
    {
        // ====================================================
        // Partie inactivity de ActionTick().
        // ====================================================

        if (
            state.options.inactivityWarningTime <= 0)
        {
            return;
        }


        if (
            state.numberOfPendingPhases == 0)
        {
            return;
        }


        const GamePhase phase =
            phases::current(state).phase;


        if (
            phase == GamePhase::AddingNewPlayers ||
            phase == GamePhase::Configuration ||
            phase == GamePhase::PickingStartingOrder ||
            phase == GamePhase::GameFinished ||
            phase ==
                GamePhase::
                    CollectAIParametersForSave)
        {
            return;
        }


        for (PlayerNumber player = 0;
             player < state.numberOfPlayers;
             ++player)
        {
            PlayerState& info =
                state.players[player];


            const std::uint64_t lastActivity =
                info.timeOfLastActivity;


            if (
                state.gameDurationInSeconds <
                lastActivity)
            {
                continue;
            }


            if (
                state.gameDurationInSeconds -
                    lastActivity <
                static_cast<std::uint64_t>(
                    state.options
                        .inactivityWarningTime
                ))
            {
                continue;
            }


            ++info.inactivityCount;


            info.timeOfLastActivity =
                static_cast<std::uint32_t>(
                    state.gameDurationInSeconds
                );


            if (info.inactivityCount < 6)
            {
                // Les deux premiers avertissements ne sont
                // volontairement pas affichés.

                if (info.inactivityCount >= 3)
                {
                    messaging::sendAction(
                        actions::Type::
                            NotifyErrorMessage,
                        BankPlayer,
                        AllPlayers,
                        legacy_text::
                            ErrorPlayerInactiveWarning,

                        info.inactivityCount *
                            state.options
                                .inactivityWarningTime,

                        player,

                        (
                            6 -
                            info.inactivityCount
                        ) *
                            state.options
                                .inactivityWarningTime
                    );
                }


                continue;
            }


            // ------------------------------------------------
            // Sixième expiration :
            // remplacement automatique par IA.
            // ------------------------------------------------

            info.inactivityCount = 0;


            info.timeOfLastActivity =
                static_cast<std::uint32_t>(
                    state.gameDurationInSeconds
                );


            messaging::sendAction(
                actions::Type::
                    DisconnectedPlayer,
                BankPlayer,
                BankPlayer,
                player,
                0,
                0,
                0
            );
        }
    }


    bool restartLifecyclePhase(
        GameState& state)
    {
        switch (
            phases::current(state).phase)
        {
            case GamePhase::GameFinished:
            {
                // GF_GAME_FINISHED original.

                messaging::sendAction(
                    actions::Type::NotifyGameOver,
                    BankPlayer,
                    AllPlayers,
                    phases::current(state)
                        .fromPlayer,
                    phases::current(state)
                        .amount,
                    0,
                    0
                );


                return true;
            }


            case GamePhase::Paused:
            {
                // GF_PAUSED original.

                messaging::sendAction(
                    actions::Type::NotifyGamePaused,
                    BankPlayer,
                    AllPlayers,
                    0,
                    0,
                    0,
                    0
                );


                return true;
            }


            default:
                return false;
        }
    }
}
