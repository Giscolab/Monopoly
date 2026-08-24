#include "RuleResync.hpp"

#include "BoardRules.hpp"
#include "Messaging.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace monopoly::rules::resync
{
    namespace
    {
        void putU8(
            std::vector<std::uint8_t>& data,
            std::uint8_t value)
        {
            data.push_back(value);
        }


        void putU32(
            std::vector<std::uint8_t>& data,
            std::uint32_t value)
        {
            for (int shift = 0;
                 shift < 32;
                 shift += 8)
            {
                putU8(
                    data,
                    static_cast<std::uint8_t>(
                        value >> shift
                    )
                );
            }
        }


        void putI64(
            std::vector<std::uint8_t>& data,
            std::int64_t value)
        {
            const std::uint64_t raw =
                static_cast<std::uint64_t>(
                    value
                );

            for (int shift = 0;
                 shift < 64;
                 shift += 8)
            {
                putU8(
                    data,
                    static_cast<std::uint8_t>(
                        raw >> shift
                    )
                );
            }
        }


        std::vector<std::uint8_t>
        makeClientState(
            const GameState& state,
            Cause cause)
        {
            // Version moderne et explicitement packed du
            // RULE_ClientResyncInfoRecord.

            std::vector<std::uint8_t> data;

            putU8(data, 1); // format version


            // Cash.
            for (PlayerNumber player = 0;
                 player < MaxPlayers;
                 ++player)
            {
                putI64(
                    data,
                    state.players[player].cash
                );
            }


            // Property sets.
            std::uint32_t mortgaged = 0;

            std::uint32_t
                owned[MaxPlayers]{};


            for (std::size_t squareNo = 0;
                 squareNo <= 39;
                 ++squareNo)
            {
                const SquareState& square =
                    state.squares[squareNo];


                if (
                    square.owner >=
                    MaxPlayers)
                {
                    continue;
                }


                const std::uint32_t bit =
                    board::propertyBit(
                        static_cast<
                            board::SquareType
                        >(squareNo)
                    );


                owned[
                    square.owner
                ] |= bit;


                if (square.mortgaged)
                {
                    mortgaged |= bit;
                }
            }


            for (const auto properties :
                 owned)
            {
                putU32(
                    data,
                    properties
                );
            }


            putU32(
                data,
                mortgaged
            );


            // Player squares.
            for (PlayerNumber player = 0;
                 player < MaxPlayers;
                 ++player)
            {
                putU8(
                    data,
                    state.players[player]
                        .currentSquare
                );
            }


            // Jail card owners.
            for (const CardDeck& deck :
                 state.cards)
            {
                putU8(
                    data,
                    deck.jailOwner
                );
            }


            // Buildings.
            for (const SquareState& square :
                 state.squares)
            {
                putU8(
                    data,
                    square.houses
                );
            }


            putU8(
                data,
                static_cast<std::uint8_t>(
                    cause
                )
            );


            const GamePhase currentPhase =
                state.numberOfPendingPhases > 0
                    ? state.phaseStack[0].phase
                    : GamePhase::AddingNewPlayers;


            putU8(
                data,
                static_cast<std::uint8_t>(
                    currentPhase
                )
            );


            std::uint32_t firstMoves = 0;


            for (PlayerNumber player = 0;
                 player <
                    state.numberOfPlayers;
                 ++player)
            {
                if (
                    state.players[player]
                        .firstMoveMade)
                {
                    firstMoves |=
                        1u << player;
                }
            }


            putU32(
                data,
                firstMoves
            );


            putU8(
                data,
                state.currentPlayer
            );


            return data;
        }
    }


    void sendAll(
        const GameState& state,
        PlayerNumber toPlayer,
        Cause cause,
        const archive::AIStateArray& aiStates)
    {
        // SendResyncMessages() original.

        if (
            toPlayer >=
            state.numberOfPlayers)
        {
            toPlayer =
                AllPlayers;
        }


        // ----------------------------------------------------
        // Nombre de joueurs.
        // ----------------------------------------------------

        messaging::sendAction(
            actions::Type::NotifyNumberOfPlayers,
            BankPlayer,
            toPlayer,
            state.numberOfPlayers,
            0,
            0,
            0
        );


        // ----------------------------------------------------
        // Configuration.
        // ----------------------------------------------------

        actions::Message configuration{};

        configuration.action =
            actions::Type::
                NotifyProposedConfiguration;

        configuration.fromPlayer =
            BankPlayer;

        configuration.toPlayer =
            toPlayer;

        configuration.numberA =
            NobodyPlayer;

        configuration.numberB = 0;

        // Star Wars Monopoly protocol version :
        // 1 = futures support.
        configuration.numberC = 1;


        archive::encodeOptions(
            state.options,
            configuration.binaryDataA
        );


        messaging::sendAction(
            configuration
        );


        // ----------------------------------------------------
        // Players + AI state.
        // ----------------------------------------------------

        for (PlayerNumber player = 0;
             player <
                state.numberOfPlayers;
             ++player)
        {
            const PlayerState& info =
                state.players[player];


            messaging::sendAction(
                actions::Type::NotifyNamePlayer,
                BankPlayer,
                toPlayer,
                player,
                info.token,
                info.colour,
                info.aiPlayerLevel,
                info.name
            );


            if (
                !aiStates[player].empty())
            {
                actions::Message ai{};

                ai.action =
                    actions::Type::
                        NotifyAIParameters;

                ai.fromPlayer =
                    BankPlayer;

                ai.toPlayer =
                    toPlayer;

                ai.numberA =
                    player;

                ai.binaryDataA =
                    aiStates[player];


                messaging::sendAction(ai);
            }
        }


        // ----------------------------------------------------
        // Compact game state.
        // ----------------------------------------------------

        actions::Message stateMessage{};

        stateMessage.action =
            actions::Type::
                NotifyClientResyncInfo;

        stateMessage.fromPlayer =
            BankPlayer;

        stateMessage.toPlayer =
            toPlayer;

        stateMessage.binaryDataA =
            makeClientState(
                state,
                cause
            );


        messaging::sendAction(
            stateMessage
        );


        // ----------------------------------------------------
        // Futures / immunités actifs.
        // ----------------------------------------------------

        for (const CountHitRecord& hit :
             state.countHits)
        {
            if (
                hit.toPlayer ==
                    NobodyPlayer ||
                hit.tradedItem)
            {
                continue;
            }


            actions::Message update{};

            update.action =
                hit.hitType ==
                    CountHitType::RentImmunity
                    ? actions::Type::
                        NotifyImmunityCount
                    : actions::Type::
                        NotifyFutureRentCount;

            update.fromPlayer =
                BankPlayer;

            update.toPlayer =
                toPlayer;

            update.numberA =
                hit.toPlayer;

            update.numberB =
                hit.hitCount;

            // SQ_OFF_BOARD :
            // update global, pas consommation d'un hit.
            update.numberC = 41;

            update.numberD =
                hit.fromPlayer;

            update.numberE =
                hit.properties;


            messaging::sendAction(
                update
            );
        }


        // Réémettre le prompt de la phase courante.
        messaging::sendAction(
            actions::Type::RestartPhase,
            BankPlayer,
            BankPlayer
        );
    }
}
