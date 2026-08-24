#include "PhaseStack.hpp"

namespace monopoly::rules::phases
{
    namespace
    {
        constexpr PendingPhase fallbackPhase
        {
            GamePhase::AddingNewPlayers,
            0,
            0,
            0
        };
    }

    bool push(
        GameState& state,
        GamePhase phase,
        PlayerNumber fromPlayer,
        PlayerNumber toPlayer,
        std::int64_t amount)
    {
        // PushPhase() original.
        if (state.numberOfPendingPhases >= MaxPendingPhases)
        {
            return false;
        }

        // L'original décale toute la pile vers le bas afin que
        // l'élément 0 reste toujours la phase courante.
        for (std::size_t i = state.numberOfPendingPhases; i >= 1; --i)
        {
            state.phaseStack[i] = state.phaseStack[i - 1];
        }

        ++state.numberOfPendingPhases;

        state.phaseStack[0] =
        {
            phase,
            fromPlayer,
            toPlayer,
            amount
        };

        // StackedRulesStates[] sera porté avec le système
        // de sauvegarde/reprise de phase qui l'utilise réellement.

        return true;
    }

    bool pop(GameState& state)
    {
        // PopPhase() original.
        if (state.numberOfPendingPhases < 1)
        {
            state.numberOfPendingPhases = 0;

            push(
                state,
                GamePhase::AddingNewPlayers,
                0,
                0,
                0
            );

            return false;
        }

        --state.numberOfPendingPhases;

        for (std::size_t i = 0;
             i < state.numberOfPendingPhases;
             ++i)
        {
            state.phaseStack[i] =
                state.phaseStack[i + 1];
        }

        return true;
    }

    void switchTo(
        GameState& state,
        GamePhase phase,
        PlayerNumber fromPlayer,
        PlayerNumber toPlayer,
        std::int64_t amount)
    {
        // SwitchPhase() original.
        if (state.numberOfPendingPhases < 1)
        {
            state.numberOfPendingPhases = 1;
        }

        state.phaseStack[0] =
        {
            phase,
            fromPlayer,
            toPlayer,
            amount
        };
    }

    const PendingPhase& current(const GameState& state)
    {
        // Équivalent moderne du macro :
        //
        // #define CurrentPhaseM
        //     (CurrentRulesState.phaseStack[0].phase)

        if (state.numberOfPendingPhases == 0)
        {
            return fallbackPhase;
        }

        return state.phaseStack[0];
    }
}
