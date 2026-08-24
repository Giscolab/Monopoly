#include "RuleSave.hpp"

#include "BoardRules.hpp"
#include "LegacyTextIds.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"
#include "RuleArchive.hpp"
#include "RuleResync.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace monopoly::rules::save
{
    namespace
    {
        archive::AIStateArray collectedAIStates{};


        void clearAIStates()
        {
            for (auto& state :
                 collectedAIStates)
            {
                state.clear();
            }
        }


        void completed(
            const actions::Message& message,
            bool success)
        {
            messaging::sendAction(
                actions::Type::
                    NotifyActionCompleted,
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


        void errorWrongPlayer(
            const GameState& state,
            const actions::Message& message)
        {
            completed(
                message,
                false
            );


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
                state.numberOfPendingPhases > 0
                    ? static_cast<std::int64_t>(
                        phases::current(state).phase
                    )
                    : 0
            );
        }


        void errorWrongPhase(
            const GameState& state,
            const actions::Message& message)
        {
            completed(
                message,
                false
            );


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
                state.numberOfPendingPhases > 0
                    ? static_cast<std::int64_t>(
                        phases::current(state).phase
                    )
                    : 0
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


        bool unsafeExternalUndoState(
            const GameState& state)
        {
            // Le source 1999 vérifie StackedRulesStates[].
            //
            // Dans notre port, ces snapshots externes existent
            // actuellement dans CollectingPayment et
            // DecomposeHotel.
            //
            // On interdit donc la sauvegarde dans ces phases
            // tant qu'ils n'ont pas été intégrés directement
            // au GameState sérialisable.

            for (std::size_t i = 0;
                 i <
                    state.numberOfPendingPhases;
                 ++i)
            {
                const GamePhase phase =
                    state.phaseStack[i].phase;


                if (
                    phase ==
                        GamePhase::CollectingPayment ||
                    phase ==
                        GamePhase::DecomposeHotel)
                {
                    return true;
                }
            }


            return false;
        }


        bool validRiffBlob(
            const std::vector<std::uint8_t>& data,
            std::size_t& usefulSize)
        {
            usefulSize = 0;


            if (data.size() < 8)
            {
                return false;
            }


            const std::uint32_t chunkSize =
                static_cast<std::uint32_t>(
                    data[4]
                ) |
                (
                    static_cast<std::uint32_t>(
                        data[5]
                    ) << 8
                ) |
                (
                    static_cast<std::uint32_t>(
                        data[6]
                    ) << 16
                ) |
                (
                    static_cast<std::uint32_t>(
                        data[7]
                    ) << 24
                );


            const std::uint64_t total =
                static_cast<std::uint64_t>(
                    chunkSize
                ) + 8ull;


            if (
                total > data.size())
            {
                return false;
            }


            usefulSize =
                static_cast<std::size_t>(
                    total
                );


            return true;
        }
    }


    void resetTransientState()
    {
        clearAIStates();
    }


    void actionGetGameState(
        GameState& state,
        const actions::Message& message)
    {
        // ActionGetGameState() original.

        if (
            state.numberOfPendingPhases == 0)
        {
            errorWrongPhase(
                state,
                message
            );

            return;
        }


        const GamePhase current =
            phases::current(state).phase;


        // Une sauvegarde est déjà en cours.
        if (
            current ==
                GamePhase::
                    CollectAIParametersForSave)
        {
            errorWrongPhase(
                state,
                message
            );

            return;
        }


        // Phases où Restart n'est pas sûr.
        if (
            current ==
                GamePhase::WaitStartTurn ||
            current ==
                GamePhase::MovingToken ||
            current ==
                GamePhase::
                    WaitForEverybodyReady)
        {
            errorWrongPhase(
                state,
                message
            );

            return;
        }


        // Seuls les vrais slots joueur peuvent recevoir
        // le blob de sauvegarde.
        if (
            message.fromPlayer >=
            MaxPlayers)
        {
            errorWrongPlayer(
                state,
                message
            );

            return;
        }


        // Shared temporary state non sérialisable.
        if (
            state.tradeInProgress ||
            current == GamePhase::Auction)
        {
            errorWrongPhase(
                state,
                message
            );

            return;
        }


        // Equivalent moderne de StackedRulesStates[].valid.
        if (
            unsafeExternalUndoState(state))
        {
            completed(
                message,
                false
            );


            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorSaveGameLater,
                0,
                message.fromPlayer,
                0
            );


            return;
        }


        clearAIStates();


        std::uint32_t waitingForAI = 0;


        for (PlayerNumber player = 0;
             player <
                state.numberOfPlayers;
             ++player)
        {
            if (
                state.players[player]
                    .aiPlayerLevel != 0)
            {
                waitingForAI |=
                    1u << player;
            }
        }


        phases::push(
            state,
            GamePhase::
                CollectAIParametersForSave,

            static_cast<PlayerNumber>(
                waitingForAI
            ),

            message.fromPlayer,

            static_cast<std::int64_t>(
                state.gameDurationInSeconds
            )
        );


        sendRestart();
    }


    void actionAISaveParameters(
        GameState& state,
        const actions::Message& message)
    {
        // ActionAISaveParameters() original.

        if (
            state.numberOfPendingPhases == 0 ||
            phases::current(state).phase !=
                GamePhase::
                    CollectAIParametersForSave)
        {
            errorWrongPhase(
                state,
                message
            );

            return;
        }


        const PlayerNumber player =
            message.fromPlayer;


        if (
            player >=
                state.numberOfPlayers ||
            state.players[player]
                .aiPlayerLevel == 0)
        {
            errorWrongPlayer(
                state,
                message
            );

            return;
        }


        // Retirer l'AI du set attendu.
        std::uint32_t waitingSet =
            phases::current(state)
                .fromPlayer;


        waitingSet &=
            ~(1u << player);


        state.phaseStack[0]
            .fromPlayer =
            static_cast<PlayerNumber>(
                waitingSet
            );


        collectedAIStates[player]
            .clear();


        // L'ancien moteur sauvegardait les paramètres IA
        // sous forme RIFF opaque.
        //
        // On conserve exactement cette propriété :
        // RULE n'interprète pas leur contenu.

        std::size_t usefulSize = 0;


        if (
            validRiffBlob(
                message.binaryDataA,
                usefulSize
            ))
        {
            collectedAIStates[player]
                .assign(
                    message.binaryDataA.begin(),
                    message.binaryDataA.begin() +
                        static_cast<std::ptrdiff_t>(
                            usefulSize
                        )
                );
        }


        // Pas de restart intermédiaire.
        // Seulement lorsque le dernier AI a répondu.
        if (waitingSet == 0)
        {
            sendRestart();
        }
    }


    void actionSetGameState(
        GameState& state,
        const actions::Message& message)
    {
        // ActionSetGameState() original.

        if (
            state.numberOfPlayers > 0 &&
            message.fromPlayer >=
                state.numberOfPlayers)
        {
            errorWrongPlayer(
                state,
                message
            );

            return;
        }


        GameState testState{};

        archive::AIStateArray
            testAIStates{};


        // Premier décodage :
        // valider avant de toucher à l'état courant.
        if (
            !archive::decodeSave(
                message.binaryDataA,
                testState,
                &testAIStates
            ))
        {
            completed(
                message,
                false
            );


            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                message.fromPlayer,
                legacy_text::ErrorLoadGameFailure,
                0,
                message.fromPlayer,
                0
            );


            return;
        }


        // ----------------------------------------------------
        // Commit.
        // ----------------------------------------------------

        state =
            std::move(testState);


        collectedAIStates =
            std::move(testAIStates);


        completed(
            message,
            true
        );


        // Réinitialisation des consommateurs UI.
        messaging::sendAction(
            actions::Type::NotifyNumberOfPlayers,
            BankPlayer,
            AllPlayers,
            0,
            message.numberB,
            0,
            0
        );


        // ----------------------------------------------------
        // Tous les joueurs deviennent locaux.
        // ----------------------------------------------------

        bool humanFound = false;

        PlayerNumber lastNonbankruptAI = 0;


        for (PlayerNumber player = 0;
             player <
                state.numberOfPlayers;
             ++player)
        {
            if (
                state.players[player]
                    .currentSquare < 41)
            {
                if (
                    state.players[player]
                        .aiPlayerLevel == 0)
                {
                    humanFound = true;
                }
                else
                {
                    lastNonbankruptAI =
                        player;
                }
            }


            messaging::sendAction(
                actions::Type::
                    NotifyAddLocalPlayer,
                BankPlayer,
                AllPlayers,
                player,
                0,
                0,
                0,
                state.players[player].name
            );
        }


        // Original :
        // si la sauvegarde ne contient plus aucun humain,
        // forcer un AI survivant en humain.
        if (
            !humanFound &&
            state.numberOfPlayers > 0)
        {
            state.players[
                lastNonbankruptAI
            ].aiPlayerLevel = 0;
        }


        // Réémettre tout l'état observable.
        resync::sendAll(
            state,
            AllPlayers,
            resync::Cause::LoadGame,
            collectedAIStates
        );


        // Recalcul des données prédéfinies dépendant du
        // short-game housesPerHotel.
        board::initializeForOptions(
            state.options
        );
    }


    void actionGetOptionsForSave(
        GameState& state,
        const actions::Message& message)
    {
        // ActionGetOptionsForSave() original.

        if (
            message.fromPlayer >=
            MaxPlayers)
        {
            errorWrongPlayer(
                state,
                message
            );

            return;
        }


        actions::Message result{};

        result.action =
            actions::Type::
                NotifyOptionsForSave;

        result.fromPlayer =
            BankPlayer;

        result.toPlayer =
            message.fromPlayer;


        if (
            !archive::encodeOptions(
                state.options,
                result.binaryDataA
            ))
        {
            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                message.fromPlayer,
                legacy_text::
                    ErrorSaveOptionsFailure,
                0,
                message.fromPlayer,
                0
            );


            return;
        }


        messaging::sendAction(
            result
        );
    }


    void actionResyncClient(
        GameState& state,
        const actions::Message& message)
    {
        // ActionResyncClient() original.

        resync::sendAll(
            state,
            static_cast<PlayerNumber>(
                message.numberA
            ),
            resync::Cause::NetworkRefresh,
            collectedAIStates
        );
    }


    bool restartSavePhase(
        GameState& state)
    {
        if (
            state.numberOfPendingPhases == 0 ||
            phases::current(state).phase !=
                GamePhase::
                    CollectAIParametersForSave)
        {
            return false;
        }


        const PendingPhase savePhase =
            phases::current(state);


        // ----------------------------------------------------
        // Demander les états AI.
        // ----------------------------------------------------

        messaging::sendAction(
            actions::Type::
                NotifyAINeedParametersForSave,
            BankPlayer,
            AllPlayers,
            savePhase.fromPlayer,
            savePhase.toPlayer,
            0,
            0
        );


        if (
            savePhase.fromPlayer != 0)
        {
            return true;
        }


        // ----------------------------------------------------
        // Tous les AI ont répondu.
        //
        // Pop AVANT sérialisation :
        // GF_COLLECT_AI_PARAMETERS_FOR_SAVE ne doit pas être
        // présent dans le fichier sauvegardé.
        // ----------------------------------------------------

        const PlayerNumber requester =
            savePhase.toPlayer;


        phases::pop(state);

        sendRestart();


        actions::Message result{};

        result.action =
            actions::Type::
                NotifyGameStateForSave;

        result.fromPlayer =
            BankPlayer;

        result.toPlayer =
            requester;


        if (
            !archive::encodeSave(
                state,
                collectedAIStates,
                result.binaryDataA
            ))
        {
            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::
                    ErrorSaveGameFailure,
                0,
                requester,
                0
            );


            clearAIStates();

            return true;
        }


        messaging::sendAction(
            result
        );


        clearAIStates();


        return true;
    }


    void onIdleTick(
        GameState& state)
    {
        // ActionTick() :
        // timeout GF_COLLECT_AI_PARAMETERS_FOR_SAVE.

        if (
            state.numberOfPendingPhases == 0 ||
            phases::current(state).phase !=
                GamePhase::
                    CollectAIParametersForSave)
        {
            return;
        }


        const PendingPhase savePhase =
            phases::current(state);


        if (
            state.gameDurationInSeconds <
            static_cast<std::uint64_t>(
                savePhase.amount
            ))
        {
            return;
        }


        if (
            state.gameDurationInSeconds -
                static_cast<std::uint64_t>(
                    savePhase.amount
                ) <= 100)
        {
            return;
        }


        messaging::sendAction(
            actions::Type::NotifyErrorMessage,
            BankPlayer,
            AllPlayers,
            legacy_text::ErrorSaveGameFailure,
            0,
            savePhase.toPlayer,
            0
        );


        clearAIStates();


        phases::pop(state);

        sendRestart();
    }
}

