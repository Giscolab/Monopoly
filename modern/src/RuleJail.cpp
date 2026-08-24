#include "RuleJail.hpp"

#include "CardDeckRuntime.hpp"
#include "LegacyTextIds.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"
#include "RuleEconomy.hpp"

namespace monopoly::rules::jail
{
    namespace
    {
        void actionCompleted(
            const actions::Message& message,
            bool success,
            std::int64_t choice)
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
                choice
            );
        }


        void sendRestart()
        {
            messaging::sendAction(
                actions::Type::RestartPhase,
                BankPlayer,
                BankPlayer
            );
        }
    }


    void actionExitJailDecision(
        GameState& state,
        const actions::Message& message)
    {
        // ActionExitJailDecision() original.

        if (
            phases::current(state).phase !=
            GamePhase::JailRollOrPayOrCardDecision)
        {
            actionCompleted(
                message,
                false,
                0
            );

            return;
        }


        if (
            message.fromPlayer !=
                state.currentPlayer ||
            state.currentPlayer >=
                state.numberOfPlayers)
        {
            actionCompleted(
                message,
                false,
                0
            );

            return;
        }


        PlayerState& player =
            state.players[
                state.currentPlayer
            ];


        // ----------------------------------------------------
        // 0 : tenter un double.
        //
        // turnsInJail == 99 signifie :
        // paiement obligatoire, aucun lancer autorisé.
        // ----------------------------------------------------

        if (
            message.numberA == 0 &&
            player.turnsInJail < 99)
        {
            actionCompleted(
                message,
                true,
                message.numberA
            );


            phases::switchTo(
                state,
                GamePhase::WaitJailRoll,
                0,
                0,
                0
            );


            sendRestart();

            return;
        }


        // ----------------------------------------------------
        // 1 : payer les frais de sortie.
        //
        // Même si le cash est insuffisant, l'original crée
        // quand même la dette : le joueur peut alors vendre,
        // hypothéquer ou faire faillite.
        // ----------------------------------------------------

        if (message.numberA == 1)
        {
            actionCompleted(
                message,
                true,
                message.numberA
            );


            if (
                player.cash <
                state.options.getOutOfJailFee)
            {
                messaging::sendAction(
                    actions::Type::NotifyErrorMessage,
                    BankPlayer,
                    AllPlayers,
                    legacy_text::ErrorNeedJailMoney,
                    state.options.getOutOfJailFee,
                    state.currentPlayer,
                    player.cash
                );
            }


            // Lorsque la dette sera terminée,
            // ACTION_RESTART_PHASE retombera sur
            // GF_GET_OUT_OF_JAIL.

            phases::switchTo(
                state,
                GamePhase::GetOutOfJail,
                0,
                0,
                0
            );


            economy::stackDebtAndRestart(
                state,
                state.currentPlayer,
                BankPlayer,
                state.options.getOutOfJailFee
            );


            economy::addMoneyToFreeParkingPot(
                state,
                state.options.getOutOfJailFee
            );


            return;
        }


        // ----------------------------------------------------
        // 2 : utiliser Get Out of Jail Free.
        //
        // Interdit lorsque turnsInJail == 99.
        // ----------------------------------------------------

        const bool chanceCard =
            state.cards[
                static_cast<std::size_t>(
                    DeckType::Chance
                )
            ].jailOwner ==
            state.currentPlayer;


        const bool communityCard =
            state.cards[
                static_cast<std::size_t>(
                    DeckType::Community
                )
            ].jailOwner ==
            state.currentPlayer;


        if (
            message.numberA == 2 &&
            player.turnsInJail < 99 &&
            (chanceCard || communityCard))
        {
            actionCompleted(
                message,
                true,
                message.numberA
            );


            if (chanceCard)
            {
                cardruntime::transferGetOutOfJail(
                    state,
                    state.currentPlayer,
                    NobodyPlayer,
                    DeckType::Chance
                );
            }
            else
            {
                cardruntime::transferGetOutOfJail(
                    state,
                    state.currentPlayer,
                    NobodyPlayer,
                    DeckType::Community
                );
            }


            phases::switchTo(
                state,
                GamePhase::GetOutOfJail,
                0,
                0,
                0
            );


            sendRestart();

            return;
        }


        // ----------------------------------------------------
        // Choix impossible.
        //
        // Rule.cpp fait exactement :
        //
        // TMN_ERROR_NEED_JAIL_ROLL + numberA
        //
        // 0 -> roll
        // 1 -> money
        // 2 -> card
        // ----------------------------------------------------

        actionCompleted(
            message,
            false,
            0
        );


        sendRestart();


        messaging::sendAction(
            actions::Type::NotifyErrorMessage,
            BankPlayer,
            AllPlayers,
            legacy_text::ErrorNeedJailRoll +
                message.numberA,
            state.options.getOutOfJailFee,
            state.currentPlayer,
            player.cash
        );
    }
}
