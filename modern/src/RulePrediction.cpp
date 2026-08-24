#include "RulePrediction.hpp"

#include "Actions.hpp"
#include "BoardRules.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"
#include "RuleRandom.hpp"

#include <cstddef>
#include <cstdint>

namespace monopoly::rules::prediction
{
    namespace
    {
        constexpr std::uint8_t GoSquare = 0;
        constexpr std::uint8_t JustVisitingSquare = 10;
        constexpr std::uint8_t ElectricCompany = 12;
        constexpr std::uint8_t PennsylvaniaRailroad = 15;
        constexpr std::uint8_t IllinoisAvenue = 24;
        constexpr std::uint8_t BAndORailroad = 25;
        constexpr std::uint8_t WaterWorks = 28;
        constexpr std::uint8_t ShortLineRailroad = 35;
        constexpr std::uint8_t ReadingRailroad = 5;
        constexpr std::uint8_t StCharlesPlace = 11;
        constexpr std::uint8_t BoardwalkSquare = 39;
        constexpr std::uint8_t InJailSquare = 40;
        constexpr std::uint8_t OffBoardSquare = 41;


        bool predictionPhase(
            GamePhase phase)
        {
            switch (phase)
            {
                case GamePhase::WaitMoveRoll:
                case GamePhase::WaitJailRoll:
                case GamePhase::WaitUtilityRoll:
                case GamePhase::WaitUntilCardSeen:
                case GamePhase::
                    JailRollOrPayOrCardDecision:
                case GamePhase::
                    FlatOrFractionTaxDecision:
                case GamePhase::
                    AuctionOrBuyDecision:
                    return true;

                default:
                    return false;
            }
        }


        PlayerNumber predictPlayerWhoMovesNext(
            const GameState& state)
        {
            if (
                state.numberOfPendingPhases == 0 ||
                !predictionPhase(
                    phases::current(state).phase
                ))
            {
                return NobodyPlayer;
            }


            PlayerNumber player =
                state.currentPlayer;


            if (
                player >=
                state.numberOfPlayers)
            {
                return NobodyPlayer;
            }


            // Premier lancer :
            // 0 == 0.
            //
            // Double :
            // même joueur.
            if (
                state.dice[0] ==
                state.dice[1])
            {
                return player;
            }


            while (true)
            {
                ++player;


                if (
                    player >=
                    state.numberOfPlayers)
                {
                    player = 0;
                }


                if (
                    player ==
                    state.currentPlayer)
                {
                    break;
                }


                if (
                    state.players[player]
                        .currentSquare <
                    OffBoardSquare)
                {
                    break;
                }
            }


            return player;
        }


        std::uint8_t randomDie()
        {
            return
                static_cast<std::uint8_t>(
                    (
                        random::generator()() %
                        6u
                    ) + 1u
                );
        }


        void ensureNextDice(
            GameState& state)
        {
            if (state.nextDice[0] != 0)
            {
                return;
            }


            state.nextDice[0] =
                randomDie();


            // Exactement les trois appels de séparation
            // du code original.
            (void) random::generator()();
            (void) random::generator()();
            (void) random::generator()();


            state.nextDice[1] =
                randomDie();
        }


        std::uint8_t previewCard(
            const GameState& state,
            std::uint8_t squareNo)
        {
            if (squareNo >= SquareCount)
            {
                return 0;
            }


            const auto group =
                board::definition(
                    static_cast<board::SquareType>(
                        squareNo
                    )
                ).group;


            if (
                group ==
                board::SquareGroup::CommunityChest)
            {
                return
                    state.cards[
                        static_cast<std::size_t>(
                            DeckType::Community
                        )
                    ].cardPile[0];
            }


            if (
                group ==
                board::SquareGroup::Chance)
            {
                return
                    state.cards[
                        static_cast<std::size_t>(
                            DeckType::Chance
                        )
                    ].cardPile[0];
            }


            return 0;
        }
    }


    void sendNextMove(
        GameState& state)
    {
        // ====================================================
        // PredictNextMove() original.
        // ====================================================

        if (
            state.numberOfPendingPhases == 0)
        {
            return;
        }


        PlayerNumber player =
            state.currentPlayer;


        if (
            player >=
            state.numberOfPlayers)
        {
            return;
        }


        std::uint8_t squareNo =
            state.players[player]
                .currentSquare;


        if (squareNo >= OffBoardSquare)
        {
            return;
        }


        PlayerNumber movingPlayer =
            player;


        std::uint8_t movingFromSquare =
            squareNo;


        std::uint8_t movingToSquare =
            OffBoardSquare;


        actions::Type movingAction =
            actions::Type::Null;


        ensureNextDice(state);


        const std::uint8_t totalRolled =
            static_cast<std::uint8_t>(
                state.nextDice[0] +
                state.nextDice[1]
            );


        const bool doublesRolled =
            state.nextDice[0] ==
            state.nextDice[1];


        const GamePhase phase =
            phases::current(state).phase;


        // ----------------------------------------------------
        // Carte en cours de lecture.
        // ----------------------------------------------------

        if (
            phase ==
            GamePhase::WaitUntilCardSeen)
        {
            const auto group =
                board::definition(
                    static_cast<board::SquareType>(
                        squareNo
                    )
                ).group;


            const DeckType deck =
                group ==
                    board::SquareGroup::
                        CommunityChest
                    ? DeckType::Community
                    : DeckType::Chance;


            const CardType card =
                static_cast<CardType>(
                    state.cards[
                        static_cast<std::size_t>(
                            deck
                        )
                    ].cardPile[0]
                );


            switch (card)
            {
                case CardType::
                    CommunityGoDirectlyToJail:

                case CardType::
                    ChanceGoDirectlyToJail:

                    movingAction =
                        actions::Type::
                            NotifyJumpToSquare;

                    movingToSquare =
                        InJailSquare;

                    break;


                case CardType::
                    CommunityGoDirectlyToGo:

                case CardType::
                    ChanceGoDirectlyToGo:

                    movingAction =
                        actions::Type::
                            NotifyJumpToSquare;

                    movingToSquare =
                        GoSquare;

                    break;


                case CardType::
                    ChanceGoToReadingRailroad:

                    movingAction =
                        actions::Type::
                            NotifyMoveForwards;

                    movingToSquare =
                        ReadingRailroad;

                    break;


                case CardType::
                    ChanceGoToNearestUtility:

                    movingAction =
                        actions::Type::
                            NotifyMoveForwards;


                    if (
                        squareNo >=
                            ElectricCompany &&
                        squareNo <
                            WaterWorks)
                    {
                        movingToSquare =
                            WaterWorks;
                    }
                    else
                    {
                        movingToSquare =
                            ElectricCompany;
                    }

                    break;


                case CardType::
                    ChanceGoToBoardwalk:

                    movingAction =
                        actions::Type::
                            NotifyMoveForwards;

                    movingToSquare =
                        BoardwalkSquare;

                    break;


                case CardType::
                    ChanceGoToStCharlesPlace:

                    movingAction =
                        actions::Type::
                            NotifyMoveForwards;

                    movingToSquare =
                        StCharlesPlace;

                    break;


                case CardType::
                    ChanceGoToNearestRailroadPayDouble1:

                case CardType::
                    ChanceGoToNearestRailroadPayDouble2:

                    movingAction =
                        actions::Type::
                            NotifyMoveForwards;


                    if (
                        squareNo >=
                            ShortLineRailroad ||
                        squareNo <
                            ReadingRailroad)
                    {
                        movingToSquare =
                            ReadingRailroad;
                    }
                    else if (
                        squareNo <
                        PennsylvaniaRailroad)
                    {
                        movingToSquare =
                            PennsylvaniaRailroad;
                    }
                    else if (
                        squareNo <
                        BAndORailroad)
                    {
                        movingToSquare =
                            BAndORailroad;
                    }
                    else
                    {
                        movingToSquare =
                            ShortLineRailroad;
                    }

                    break;


                case CardType::
                    ChanceGoToIllinoisAvenue:

                    movingAction =
                        actions::Type::
                            NotifyMoveForwards;

                    movingToSquare =
                        IllinoisAvenue;

                    break;


                case CardType::
                    ChanceGoBackThreeSpaces:
                {
                    movingAction =
                        actions::Type::
                            NotifyMoveBackwards;


                    int destination =
                        static_cast<int>(
                            squareNo
                        ) - 3;


                    if (destination < 0)
                    {
                        destination +=
                            BoardwalkSquare + 1;
                    }


                    movingToSquare =
                        static_cast<std::uint8_t>(
                            destination
                        );

                    break;
                }


                default:
                {
                    player =
                        predictPlayerWhoMovesNext(
                            state
                        );


                    if (
                        player < MaxPlayers)
                    {
                        movingPlayer =
                            player;


                        movingFromSquare =
                            state.players[player]
                                .currentSquare;


                        if (
                            movingFromSquare <
                            OffBoardSquare)
                        {
                            movingAction =
                                actions::Type::
                                    NotifyMoveForwards;


                            movingToSquare =
                                static_cast<std::uint8_t>(
                                    movingFromSquare +
                                    totalRolled
                                );


                            if (
                                movingToSquare >
                                BoardwalkSquare)
                            {
                                movingToSquare -=
                                    BoardwalkSquare +
                                    1;
                            }
                        }
                    }

                    break;
                }
            }
        }

        // ----------------------------------------------------
        // Un lancer de mouvement/prison est attendu.
        // ----------------------------------------------------

        else if (
            phase ==
                GamePhase::WaitMoveRoll ||
            phase ==
                GamePhase::WaitJailRoll ||
            phase ==
                GamePhase::
                    JailRollOrPayOrCardDecision)
        {
            if (
                doublesRolled &&
                state.numberOfDoublesRolled >= 2)
            {
                movingAction =
                    actions::Type::
                        NotifyJumpToSquare;


                movingToSquare =
                    InJailSquare;
            }
            else
            {
                if (
                    squareNo ==
                    InJailSquare)
                {
                    squareNo =
                        JustVisitingSquare;
                }


                movingAction =
                    actions::Type::
                        NotifyMoveForwards;


                movingToSquare =
                    static_cast<std::uint8_t>(
                        squareNo +
                        totalRolled
                    );


                if (
                    movingToSquare >
                    BoardwalkSquare)
                {
                    movingToSquare -=
                        BoardwalkSquare + 1;
                }
            }
        }

        // ----------------------------------------------------
        // La décision en cours terminera potentiellement
        // le tour : prédire le prochain joueur.
        // ----------------------------------------------------

        else if (
            phase ==
                GamePhase::
                    FlatOrFractionTaxDecision ||
            phase ==
                GamePhase::WaitUtilityRoll ||
            phase ==
                GamePhase::
                    AuctionOrBuyDecision)
        {
            player =
                predictPlayerWhoMovesNext(
                    state
                );


            if (player < MaxPlayers)
            {
                movingPlayer =
                    player;


                movingFromSquare =
                    state.players[player]
                        .currentSquare;


                if (
                    movingFromSquare <
                    OffBoardSquare)
                {
                    movingAction =
                        actions::Type::
                            NotifyMoveForwards;


                    movingToSquare =
                        static_cast<std::uint8_t>(
                            movingFromSquare +
                            totalRolled
                        );


                    if (
                        movingToSquare >
                        BoardwalkSquare)
                    {
                        movingToSquare -=
                            BoardwalkSquare + 1;
                    }
                }
            }
        }


        if (
            movingAction ==
            actions::Type::Null)
        {
            return;
        }


        const std::uint8_t card =
            previewCard(
                state,
                movingToSquare
            );


        actions::Message notification{};

        notification.action =
            actions::Type::NotifyNextMove;

        notification.fromPlayer =
            BankPlayer;

        notification.toPlayer =
            AllPlayers;

        notification.numberA =
            static_cast<std::int64_t>(
                movingAction
            );

        notification.numberB =
            movingToSquare;

        notification.numberC =
            movingFromSquare;

        notification.numberD =
            movingPlayer;

        notification.numberE =
            card;


        messaging::sendAction(
            notification
        );
    }
}
