#include "RulePlayers.hpp"

#include "LegacyTextIds.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <string>
#include <string_view>

namespace monopoly::rules::players
{
    namespace
    {
        std::wstring messageString(
            const actions::Message& message)
        {
            return
                std::wstring(
                    message.stringA.data()
                );
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


        void sendRestart()
        {
            messaging::sendAction(
                actions::Type::RestartPhase,
                BankPlayer,
                BankPlayer
            );
        }


        void notYourPlayer(
            PlayerNumber player,
            std::wstring_view requestedName)
        {
            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorNotYourPlayer,
                0,
                player,
                0,
                requestedName
            );
        }


        void nameInUse(
            PlayerNumber player,
            std::wstring_view requestedName)
        {
            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorNameInUse,
                0,
                player,
                0,
                requestedName
            );
        }
    }


    void actionNamePlayer(
        GameState& state,
        const actions::Message& message)
    {
        const std::wstring requestedName =
            messageString(message);


        // ====================================================
        // RULE_NOBODY_PLAYER :
        // ajouter un nouveau joueur.
        // ====================================================

        if (
            message.numberA ==
            NobodyPlayer)
        {
            if (
                state.numberOfPlayers >=
                    MaxPlayers ||
                phases::current(state).phase !=
                    GamePhase::AddingNewPlayers)
            {
                messaging::sendAction(
                    actions::Type::NotifyErrorMessage,
                    BankPlayer,
                    AllPlayers,
                    legacy_text::ErrorNoMorePlayers,
                    0,
                    0,
                    0,
                    requestedName
                );

                return;
            }


            const bool badBasicRequest =
                requestedName.empty() ||
                requestedName.size() >
                    MaxPlayerNameLength ||
                message.numberC < 0 ||
                message.numberC >=
                    static_cast<std::int64_t>(
                        MaxTokens
                    ) ||
                message.numberD < 0 ||
                message.numberD >=
                    static_cast<std::int64_t>(
                        MaxPlayerColours
                    );


            PlayerNumber conflictingPlayer =
                state.numberOfPlayers;


            bool badRequest =
                badBasicRequest;


            for (PlayerNumber player = 0;
                 player <
                    state.numberOfPlayers &&
                 !badRequest;
                 ++player)
            {
                if (
                    state.players[player].token ==
                        message.numberC ||
                    state.players[player].colour ==
                        message.numberD ||
                    equalsIgnoreCase(
                        state.players[player].name,
                        requestedName
                    ))
                {
                    badRequest = true;
                    conflictingPlayer = player;
                }
            }


            if (badRequest)
            {
                nameInUse(
                    conflictingPlayer,
                    requestedName
                );

                return;
            }


            const PlayerNumber player =
                state.numberOfPlayers++;


            PlayerState& info =
                state.players[player];


            info.name =
                requestedName.substr(
                    0,
                    MaxPlayerNameLength
                );


            info.token =
                static_cast<std::uint8_t>(
                    message.numberC
                );


            info.colour =
                static_cast<std::uint8_t>(
                    message.numberD
                );


            // ActionNamePlayer ne clampait pas ce champ.
            info.aiPlayerLevel =
                static_cast<std::uint8_t>(
                    message.numberB
                );


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


            sendRestart();

            return;
        }


        // ====================================================
        // Slot existant.
        // ====================================================

        if (
            message.numberA < 0 ||
            message.numberA >=
                state.numberOfPlayers)
        {
            notYourPlayer(
                static_cast<PlayerNumber>(
                    message.numberA
                ),
                requestedName
            );

            return;
        }


        const PlayerNumber playerToReplace =
            static_cast<PlayerNumber>(
                message.numberA
            );


        // Dans le runtime actuel toutes les adresses MESS
        // sont locales. La partie NS_LOCAL de l'autorisation
        // originale est donc satisfaite.


        // ====================================================
        // Nom vide :
        // supprimer le joueur.
        // Uniquement GF_ADDING_NEW_PLAYERS.
        // ====================================================

        if (requestedName.empty())
        {
            if (
                phases::current(state).phase !=
                GamePhase::AddingNewPlayers)
            {
                nameInUse(
                    playerToReplace,
                    requestedName
                );

                return;
            }


            const PlayerState deleted =
                state.players[
                    playerToReplace
                ];


            messaging::sendAction(
                actions::Type::NotifyPlayerDeleted,
                BankPlayer,
                AllPlayers,
                playerToReplace,
                deleted.token,
                deleted.colour,
                deleted.aiPlayerLevel,
                deleted.name
            );


            const PlayerNumber lastPlayer =
                static_cast<PlayerNumber>(
                    state.numberOfPlayers - 1
                );


            if (
                playerToReplace !=
                lastPlayer)
            {
                // Le source échange le joueur supprimé
                // avec le dernier slot.

                state.players[
                    playerToReplace
                ] =
                    state.players[
                        lastPlayer
                    ];
            }


            state.players[
                lastPlayer
            ] = {};


            --state.numberOfPlayers;


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


            sendRestart();

            return;
        }


        // ====================================================
        // Renommage / takeover IA.
        // ====================================================

        PlayerState& player =
            state.players[
                playerToReplace
            ];


        if (
            !state.options.allowPlayersToTakeOverAIs &&
            player.aiPlayerLevel != 0)
        {
            notYourPlayer(
                playerToReplace,
                requestedName
            );

            return;
        }


        // Monopoly PC :
        // RULE_TAKE_OVER_AI_ONLY_WHEN_SAFE = 1.
        //
        // Le #if englobe réellement tout le chemin rename,
        // pas uniquement le cas AI.

        if (
            phases::current(state).phase !=
            GamePhase::WaitMoveRoll)
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


            if (message.fromPlayer !=
                BankPlayer)
            {
                const PlayerNumber destination =
                    message.fromPlayer <
                        MaxPlayers
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
                        phases::current(state)
                            .phase
                    )
                );


                sendRestart();
            }


            return;
        }


        const std::wstring oldName =
            player.name;


        player.name =
            requestedName.substr(
                0,
                MaxPlayerNameLength
            );


        player.aiPlayerLevel =
            static_cast<std::uint8_t>(
                message.numberB
            );


        // Token et couleur restent ceux du slot original.


        messaging::sendAction(
            actions::Type::NotifyNamePlayer,
            BankPlayer,
            AllPlayers,
            playerToReplace,
            player.token,
            player.colour,
            player.aiPlayerLevel,
            player.name
        );


        messaging::sendAction(
            actions::Type::NotifyErrorMessage,
            BankPlayer,
            AllPlayers,
            legacy_text::ErrorPlayerReplaced,
            0,
            playerToReplace,
            0,
            oldName
        );


        sendRestart();
    }
}
