#include "RuleStartupActions.hpp"
#include "RuleGameStart.hpp"
#include "RuleEconomy.hpp"
#include "RuleAuction.hpp"
#include "RuleTrade.hpp"
#include "RuleLifecycle.hpp"
#include "RuleSave.hpp"
#include "RuleSynchronization.hpp"
#include "RuleBuildings.hpp"
#include "RuleTurnActions.hpp"

#include "LegacyTextIds.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"

#include <algorithm>
#include <cwctype>
#include <string>
#include <string_view>

namespace monopoly::rules::ruleactions
{
    namespace
    {
        std::wstring messageString(
            const actions::Message& message)
        {
            return std::wstring(message.stringA.data());
        }

        bool equalsIgnoreCase(
            std::wstring_view lhs,
            std::wstring_view rhs)
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }

            for (std::size_t i = 0; i < lhs.size(); ++i)
            {
                if (std::towlower(lhs[i]) !=
                    std::towlower(rhs[i]))
                {
                    return false;
                }
            }

            return true;
        }
    }


    void afterNewGame(
        GameState& state,
        PlayerNumber initiator)
    {
        // ActionNewGame() original :
        //
        // NOTIFY_ACTION_COMPLETED(
        //     ACTION_NEW_GAME,
        //     TRUE,
        //     initiator,
        //     SavedNumberOfPlayers
        // );

        messaging::sendAction(
            actions::Type::NotifyActionCompleted,
            BankPlayer,
            AllPlayers,
            static_cast<std::int64_t>(
                actions::Type::NewGame
            ),
            1,
            initiator,
            0
        );

        // Fresh game :
        //
        // TMN_ADDING_PLAYERS,
        // RULE_MAX_PLAYERS - NumberOfPlayers

        messaging::sendAction(
            actions::Type::NotifyErrorMessage,
            BankPlayer,
            AllPlayers,
            legacy_text::AddingPlayers,
            MaxPlayers - state.numberOfPlayers,
            0,
            0
        );

        // ActionNewGame() finit en demandant le restart
        // de GF_ADDING_NEW_PLAYERS.

        messaging::sendAction(
            actions::Type::RestartPhase,
            BankPlayer,
            BankPlayer
        );
    }


    void restartPhase(
        GameState& state,
        const actions::Message& message)
    {
        // ActionRestartPhase() original.

        if (state.numberOfPendingPhases == 0)
        {
            // Source :
            // flush queue + ACTION_NEW_GAME.

            messaging::clearActionQueue();

            messaging::sendAction(
                actions::Type::NewGame,
                BankPlayer,
                AllPlayers
            );

            return;
        }

        const PendingPhase& phase =
            phases::current(state);

        switch (phase.phase)
        {
            case GamePhase::AddingNewPlayers:
            {
                // RULE_NO_SPECTATORS_ALLOWED == 0
                // dans ce build original.

                messaging::sendAction(
                    actions::Type::NotifyPleaseAddPlayers,
                    BankPlayer,
                    AllPlayers,
                    0
                );

                messaging::sendAction(
                    actions::Type::NotifyNumberOfPlayers,
                    BankPlayer,
                    AllPlayers,
                    state.numberOfPlayers
                );

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

                break;
            }

            case GamePhase::Configuration:
            {
                gamestart::restartConfiguration(
                    state
                );

                break;
            }

            default:
            {
                // Synchronisation historique :
                // GF_WAIT_FOR_EVERYBODY_READY.
                if (
                    sync::restartSyncPhase(
                        state
                    ))
                {
                    break;
                }


                // Save-game :
                // GF_COLLECT_AI_PARAMETERS_FOR_SAVE.
                if (
                    save::restartSavePhase(
                        state
                    ))
                {
                    break;
                }


                // Cycle de vie :
                // GF_GAME_FINISHED / GF_PAUSED.
                if (
                    lifecycle::restartLifecyclePhase(
                        state
                    ))
                {
                    break;
                }


                // Trade / acceptance / règlement.
                if (
                    trade::restartTradePhase(
                        state,
                        message
                    ))
                {
                    break;
                }


                // Enchères et pénuries.
                if (
                    auction::restartAuctionPhase(
                        state
                    ))
                {
                    break;
                }


                // Construction / BSSM / décomposition.
                if (
                    buildings::restartBuildingPhase(
                        state
                    ))
                {
                    break;
                }


                // D'abord les phases économiques :
                //
                // GF_COLLECTING_PAYMENT
                // GF_TRANSFER_ESCROW_PROPERTY
                // GF_FREE_UNMORTGAGE

                if (economy::restartEconomyPhase(
                        state,
                        message
                    ))
                {
                    break;
                }

                // Puis les phases du tour.

                turnactions::restartGameplayPhase(
                    state,
                    message
                );

                break;
            }
        }
    }


    void namePlayer(
        GameState& state,
        const actions::Message& message)
    {
        // Première branche réelle de ActionNamePlayer():
        //
        // numberA == RULE_NOBODY_PLAYER
        // => inscription d'un nouveau joueur.

        if (message.numberA != NobodyPlayer)
        {
            // Rename/delete/take-over AI sera porté avec
            // l'association joueur/adresse réseau originale.
            return;
        }

        if (state.numberOfPlayers >= MaxPlayers ||
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
                messageString(message)
            );

            return;
        }

        const std::wstring requestedName =
            messageString(message);

        const bool invalidName =
            requestedName.empty() ||
            requestedName.size() > MaxPlayerNameLength;

        const bool invalidToken =
            message.numberC < 0 ||
            static_cast<std::uint64_t>(message.numberC) >=
                MaxTokens;

        const bool invalidColour =
            message.numberD < 0 ||
            static_cast<std::uint64_t>(message.numberD) >=
                MaxPlayerColours;

        bool badRequest =
            invalidName ||
            invalidToken ||
            invalidColour;

        PlayerNumber conflictingPlayer =
            state.numberOfPlayers;

        for (PlayerNumber i = 0;
             i < state.numberOfPlayers && !badRequest;
             ++i)
        {
            const PlayerState& player = state.players[i];

            if (player.token ==
                    static_cast<std::uint8_t>(message.numberC) ||
                player.colour ==
                    static_cast<std::uint8_t>(message.numberD) ||
                equalsIgnoreCase(
                    player.name,
                    requestedName))
            {
                conflictingPlayer = i;
                badRequest = true;
            }
        }

        if (badRequest)
        {
            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorNameInUse,
                0,
                conflictingPlayer,
                0,
                requestedName
            );

            return;
        }

        // Ajout réel du joueur.
        const PlayerNumber playerNo =
            state.numberOfPlayers++;

        PlayerState& player =
            state.players[playerNo];

        player.name = requestedName;

        player.token =
            static_cast<std::uint8_t>(message.numberC);

        player.colour =
            static_cast<std::uint8_t>(message.numberD);

        player.aiPlayerLevel =
            static_cast<std::uint8_t>(message.numberB);

        // Source :
        // TMN_ADDING_PLAYERS,
        // RULE_MAX_PLAYERS - NumberOfPlayers

        messaging::sendAction(
            actions::Type::NotifyErrorMessage,
            BankPlayer,
            AllPlayers,
            legacy_text::AddingPlayers,
            MaxPlayers - state.numberOfPlayers
        );

        // Le source ne diffuse pas directement le joueur ici :
        // il relance GF_ADDING_NEW_PLAYERS, qui renvoie
        // toute la liste des joueurs.

        messaging::sendAction(
            actions::Type::RestartPhase,
            BankPlayer,
            BankPlayer
        );
    }
}











