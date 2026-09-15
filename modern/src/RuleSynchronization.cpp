#include "RuleSynchronization.hpp"

#include "LegacyTextIds.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"

#include <cstdint>

namespace monopoly::rules::sync
{
    namespace
    {
        // Equivalent moderne du static
        // IgnoreWaitForEverybodyReady de Rule.cpp.
        bool waitGate = false;


        void sendRestart()
        {
            messaging::sendAction(
                actions::Type::RestartPhase,
                BankPlayer,
                BankPlayer
            );
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
                message.fromPlayer
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
                message.fromPlayer
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
    }


    void resetWaitGate()
    {
        waitGate = false;
    }


    bool beginWaitOnce(
        GameState& state,
        actions::Type hint)
    {
        if (waitGate)
        {
            return false;
        }


        waitGate = true;


        waitForEverybodyReady(
            state,
            hint
        );


        return true;
    }


    void waitForEverybodyReady(
        GameState& state,
        actions::Type hint)
    {
        // WaitForEverybodyReady() original.

        std::uint32_t setToWaitFor = 0;


        for (PlayerNumber player = 0;
             player < state.numberOfPlayers;
             ++player)
        {
            if (
                state.players[player]
                    .currentSquare < 41)
            {
                setToWaitFor |=
                    1u << player;
            }
        }


        phases::push(
            state,
            GamePhase::WaitForEverybodyReady,

            static_cast<PlayerNumber>(
                setToWaitFor
            ),

            static_cast<PlayerNumber>(
                hint
            ),

            static_cast<std::int64_t>(
                state.gameDurationInSeconds
            )
        );


        sendRestart();
    }


    void actionIAmHere(
        GameState& state,
        const actions::Message& message)
    {
        // ActionIAmHere() original.

        const PlayerNumber player =
            message.fromPlayer;


        if (
            player >=
            state.numberOfPlayers)
        {
            wrongPlayer(
                state,
                message
            );

            return;
        }


        if (
            state.numberOfPendingPhases == 0 ||
            phases::current(state).phase !=
                GamePhase::WaitForEverybodyReady)
        {
            wrongPhase(
                state,
                message
            );

            return;
        }


        PendingPhase& phase =
            state.phaseStack[0];


        const std::uint32_t waitingSet =
            phase.fromPlayer;


        const std::uint32_t playerBit =
            1u << player;


        // Serial correct + joueur encore attendu.
        //
        // Duplicates et vieux messages sont ignorés,
        // comme le source.

        if (
            phase.amount ==
                message.numberA &&
            (
                waitingSet &
                playerBit
            ) != 0)
        {
            phase.fromPlayer =
                static_cast<PlayerNumber>(
                    waitingSet &
                    ~playerBit
                );


            if (phase.fromPlayer == 0)
            {
                sendRestart();
            }
        }
    }


    bool restartSyncPhase(
        GameState& state)
    {
        if (
            state.numberOfPendingPhases == 0 ||
            phases::current(state).phase !=
                GamePhase::WaitForEverybodyReady)
        {
            return false;
        }


        const PendingPhase phase =
            phases::current(state);


        messaging::sendAction(
            actions::Type::NotifyAreYouThere,
            BankPlayer,
            AllPlayers,

            phase.fromPlayer,
            phase.amount,
            phase.toPlayer,
            0
        );


        if (phase.fromPlayer == 0)
        {
            phases::pop(state);

            sendRestart();

            return true;
        }

        // Retail leaves this phase waiting after broadcasting ARE_YOU_THERE.
        // Each UI/client answers only for the slots it actually owns.  The
        // portable UserInterface now implements that boundary (with auction
        // roll-call still deferred until Pennybags reaches Begin), so RULE must
        // not synthesize I_AM_HERE for every player.  Doing so would acknowledge
        // remote slots and also duplicate local replies in the same process.
        return true;
    }


    void onIdleTick(
        GameState& state)
    {
        // ActionTick(), GF_WAIT_FOR_EVERYBODY_READY.

        if (
            state.numberOfPendingPhases == 0 ||
            phases::current(state).phase !=
                GamePhase::WaitForEverybodyReady)
        {
            return;
        }


        PendingPhase& phase =
            state.phaseStack[0];


        if (phase.amount < 0)
        {
            return;
        }


        const std::uint64_t serial =
            static_cast<std::uint64_t>(
                phase.amount
            );


        if (
            state.gameDurationInSeconds <
                serial ||
            state.gameDurationInSeconds -
                serial < 60)
        {
            return;
        }


        // Timeout :
        // ne plus attendre personne.

        phase.fromPlayer = 0;


        sendRestart();
    }
}
