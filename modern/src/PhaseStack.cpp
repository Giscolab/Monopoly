#include "PhaseStack.hpp"

#include <algorithm>
#include <tuple>
#include <utility>

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
            state.phaseUndo[i] = std::move(state.phaseUndo[i - 1]);
        }

        ++state.numberOfPendingPhases;

        state.phaseStack[0] =
        {
            phase,
            fromPlayer,
            toPlayer,
            amount
        };

        state.phaseUndo[0].reset();

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
            state.phaseUndo[i] = std::move(state.phaseUndo[i + 1]);
        }

        state.phaseUndo[state.numberOfPendingPhases].reset();

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
            state.phaseUndo[0].reset();
        }

        state.phaseStack[0] =
        {
            phase,
            fromPlayer,
            toPlayer,
            amount
        };
    }

    void saveCurrent(GameState& state)
    {
        if (state.numberOfPendingPhases == 0) return;
        auto snapshot = std::make_shared<GameState>(state);
        snapshot->phaseUndo = {};
        state.phaseUndo[0] = std::move(snapshot);
    }

    bool hasCurrentSnapshot(const GameState& state)
    {
        return state.numberOfPendingPhases > 0 && state.phaseUndo[0] != nullptr;
    }

    bool hasSnapshots(const GameState& state)
    {
        return std::any_of(state.phaseUndo.begin(),
            state.phaseUndo.begin() + state.numberOfPendingPhases,
            [](const auto& snapshot) { return snapshot != nullptr; });
    }

    bool currentStateChanged(const GameState& state)
    {
        if (!hasCurrentSnapshot(state)) return false;
        const auto& saved = *state.phaseUndo[0];
        // Compare values, not strings' storage addresses, padding or transient
        // snapshot ownership. Legacy normalizes duration only for comparison.
        const auto values = [](const GameState& value)
        {
            return std::tie(value.options, value.players, value.squares,
                value.dice, value.nextDice, value.utilityDice,
                value.numberOfDoublesRolled, value.justRolledOutOfJail,
                value.pendingCard, value.freeParkingJackpotAmount,
                value.tradeInProgress, value.countHits, value.auction,
                value.configurationProposer, value.currentPlayer,
                value.numberOfPlayers, value.phaseStack, value.numberOfPendingPhases);
        };
        if (values(state) != values(saved)) return true;
        return !std::equal(state.cards.begin(), state.cards.end(), saved.cards.begin(),
            [](const CardDeck& a, const CardDeck& b)
            {
                return std::tie(a.cardCount, a.cardPile, a.jailOwner, a.jailOfferedInTradeTo) ==
                    std::tie(b.cardCount, b.cardPile, b.jailOwner, b.jailOfferedInTradeTo);
            });
    }

    bool restoreCurrent(GameState& state)
    {
        if (!hasCurrentSnapshot(state)) return false;
        auto undo = std::move(state.phaseUndo);
        state = *undo[0];
        undo[0].reset();
        state.phaseUndo = std::move(undo);
        return true;
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
