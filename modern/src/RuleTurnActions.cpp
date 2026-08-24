#include "RuleTurnActions.hpp"

#include "BoardRules.hpp"
#include "LegacyTextIds.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"
#include "RuleEconomy.hpp"
#include "RuleAuction.hpp"
#include "RuleRandom.hpp"
#include "RuleStartingOrder.hpp"
#include "RulePrediction.hpp"
#include "RuleLifecycle.hpp"
#include "RuleCoreActions.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace monopoly::rules::turnactions
{
    namespace
    {
        constexpr std::uint8_t GoSquare = 0;
        constexpr std::uint8_t JustVisitingSquare = 10;
        constexpr std::uint8_t GoToJailSquare = 30;
        constexpr std::uint8_t BoardwalkSquare = 39;
        constexpr std::uint8_t InJailSquare = 40;
        constexpr std::uint8_t OffBoardSquare = 41;


        void sendActionCompleted(
            const actions::Message& action,
            bool success)
        {
            messaging::sendAction(
                actions::Type::NotifyActionCompleted,
                BankPlayer,
                AllPlayers,
                static_cast<std::int64_t>(action.action),
                success ? 1 : 0,
                action.fromPlayer,
                action.numberA
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


        void popAndRestart(GameState& state)
        {
            phases::pop(state);
            sendRestart();
        }


        bool validCurrentPlayer(
            const GameState& state)
        {
            return
                state.currentPlayer < state.numberOfPlayers &&
                state.players[state.currentPlayer].currentSquare <
                    OffBoardSquare;
        }


        void sendWrongPhase(
            const actions::Message& message)
        {
            sendActionCompleted(message, false);
        }


        void sendWrongPlayer(
            const actions::Message& message)
        {
            sendActionCompleted(message, false);
        }


        std::uint8_t generateDie()
        {
            return static_cast<std::uint8_t>(
                (random::generator()() % 6u) + 1u
            );
        }


        void generateDicePair(
            GameState& state,
            std::uint8_t& dieA,
            std::uint8_t& dieB)
        {
            // ActionRollDice() original pré-génère le prochain lancer.
            if (state.nextDice[0] == 0)
            {
                dieA = generateDie();

                // Source :
                // rand(); rand(); rand();
                // Reduce chance of doubles.
                (void) random::generator()();
                (void) random::generator()();
                (void) random::generator()();

                dieB = generateDie();
            }
            else
            {
                dieA = state.nextDice[0];
                dieB = state.nextDice[1];
            }

            state.nextDice[0] = generateDie();

            (void) random::generator()();
            (void) random::generator()();
            (void) random::generator()();

            state.nextDice[1] = generateDie();
        }


        std::uint8_t cardPreviewForSquare(
            const GameState& state,
            std::uint8_t squareNo)
        {
            if (squareNo >= SquareCount)
            {
                return 0;
            }

            const board::SquareDefinition& square =
                board::definition(
                    static_cast<board::SquareType>(squareNo)
                );

            if (square.group ==
                board::SquareGroup::CommunityChest)
            {
                return state.cards[
                    static_cast<std::size_t>(
                        DeckType::Community
                    )
                ].cardPile[0];
            }

            if (square.group ==
                board::SquareGroup::Chance)
            {
                return state.cards[
                    static_cast<std::size_t>(
                        DeckType::Chance
                    )
                ].cardPile[0];
            }

            return 0;
        }


        void sendMoveNotification(
            actions::Type type,
            std::uint8_t destination,
            std::uint8_t previous,
            PlayerNumber player,
            std::uint8_t card,
            bool firstMove)
        {
            actions::Message notification{};

            notification.action = type;
            notification.fromPlayer = BankPlayer;
            notification.toPlayer = AllPlayers;

            notification.numberA = destination;
            notification.numberB = previous;
            notification.numberC = player;
            notification.numberD = card;
            notification.numberE = firstMove ? 1 : 0;

            messaging::sendAction(notification);
        }


        void givePassingGoMoney(
            GameState& state,
            PlayerNumber player,
            std::int64_t amount)
        {
            if (player >= state.numberOfPlayers ||
                amount <= 0)
            {
                return;
            }

            state.players[player].cash += amount;

            messaging::sendAction(
                actions::Type::NotifyCashAnimation,
                BankPlayer,
                AllPlayers,
                BankPlayer,
                player,
                amount
            );

            messaging::sendAction(
                actions::Type::NotifyCashAmount,
                BankPlayer,
                AllPlayers,
                player,
                amount,
                state.players[player].cash
            );
        }


        void restartPreRoll(GameState& state)
        {
            if (
                state.currentPlayer >=
                state.numberOfPlayers)
            {
                return;
            }


            PlayerState& player =
                state.players[state.currentPlayer];


            if (
                player.currentSquare >=
                OffBoardSquare)
            {
                phases::switchTo(
                    state,
                    GamePhase::WaitEndTurn,
                    0,
                    0,
                    0
                );


                messaging::sendAction(
                    actions::Type::NotifyEndTurn,
                    BankPlayer,
                    AllPlayers,
                    state.currentPlayer
                );


                return;
            }

            // Le traitement des propriétés que la banque attend
            // d'enchérir sera raccordé avec le moteur d'enchères.

            // Rule.cpp GF_PREROLL :
            //
            // avant toute autre chose, la banque vend
            // les propriétés laissées en attente après
            // un refus d'achat.

            if (
                auction::
                    startPendingBankPropertyAuction(
                        state
                    ))
            {
                return;
            }


            if (player.currentSquare == InJailSquare)
            {
                if (state.dice[0] == 0)
                {
                    phases::push(
                        state,
                        GamePhase::JailRollOrPayOrCardDecision,
                        0,
                        0,
                        0
                    );

                    sendRestart();
                }
                else
                {
                    phases::switchTo(
                        state,
                        GamePhase::WaitEndTurn,
                        0,
                        0,
                        0
                    );

                    messaging::sendAction(
                        actions::Type::NotifyEndTurn,
                        BankPlayer,
                        AllPlayers,
                        state.currentPlayer
                    );
                }

                return;
            }

            // Premier lancer :
            // 0 == 0.
            //
            // Ou lancer précédent en double.
            if (state.dice[0] == state.dice[1] &&
                !state.justRolledOutOfJail)
            {
                phases::push(
                    state,
                    GamePhase::WaitMoveRoll,
                    0,
                    0,
                    0
                );

                sendRestart();
                return;
            }

            phases::switchTo(
                state,
                GamePhase::WaitEndTurn,
                0,
                0,
                0
            );

            messaging::sendAction(
                actions::Type::NotifyEndTurn,
                BankPlayer,
                AllPlayers,
                state.currentPlayer
            );
        }


        void restartWaitMoveRoll(
            GameState& state)
        {
            if (!validCurrentPlayer(state))
        {
            popAndRestart(state);
            return;
        }

            
        prediction::sendNextMove(state);
messaging::sendAction(
                actions::Type::NotifyPleaseRollDice,
                BankPlayer,
                AllPlayers,
                state.currentPlayer,
                state.players[
                    state.currentPlayer
                ].currentSquare
            );
        }


        void restartWaitEndTurn(
            GameState& state)
        {
            messaging::sendAction(
                actions::Type::NotifyEndTurn,
                BankPlayer,
                AllPlayers,
                state.currentPlayer
            );
        }


        void restartWaitUntilCardSeen(
            GameState& state)
        {
            if (!validCurrentPlayer(state))
        {
            popAndRestart(state);
            return;
        }

            
        prediction::sendNextMove(state);
const std::uint8_t squareNo =
                state.players[
                    state.currentPlayer
                ].currentSquare;

            const board::SquareDefinition& square =
                board::definition(
                    static_cast<board::SquareType>(squareNo)
                );

            const DeckType deck =
                square.group ==
                    board::SquareGroup::CommunityChest
                    ? DeckType::Community
                    : DeckType::Chance;

            const CardDeck& cards =
                state.cards[
                    static_cast<std::size_t>(deck)
                ];

            messaging::sendAction(
                actions::Type::NotifyPickedUpCard,
                BankPlayer,
                AllPlayers,
                state.currentPlayer,
                static_cast<std::int64_t>(deck),
                cards.cardPile[0]
            );
        }


        void restartBuyOrAuction(
            GameState& state)
        {
            if (!validCurrentPlayer(state))
        {
            popAndRestart(state);
            return;
        }

            
        prediction::sendNextMove(state);
const std::uint8_t squareNo =
                state.players[
                    state.currentPlayer
                ].currentSquare;

            const auto square =
                static_cast<board::SquareType>(
                    squareNo
                );

            messaging::sendAction(
                actions::Type::NotifyBuyOrAuctionDecision,
                BankPlayer,
                AllPlayers,
                state.currentPlayer,
                squareNo,
                board::definition(square).purchaseCost
            );
        }


        void restartJailDecision(
            GameState& state)
        {
            if (!validCurrentPlayer(state))
        {
            popAndRestart(state);
            return;
        }

            
        prediction::sendNextMove(state);
const PlayerState& player =
                state.players[state.currentPlayer];

            const bool canRoll =
                player.turnsInJail < 99;

            const bool canPay =
                player.cash >=
                state.options.getOutOfJailFee;

            const bool ownsJailCard =
                state.cards[
                    static_cast<std::size_t>(
                        DeckType::Chance
                    )
                ].jailOwner == state.currentPlayer ||
                state.cards[
                    static_cast<std::size_t>(
                        DeckType::Community
                    )
                ].jailOwner == state.currentPlayer;

            messaging::sendAction(
                actions::Type::NotifyJailExitChoice,
                BankPlayer,
                AllPlayers,
                state.currentPlayer,
                canRoll ? 1 : 0,
                canPay ? 1 : 0,
                (canRoll && ownsJailCard) ? 1 : 0
            );
        }


        void restartGetOutOfJail(
            GameState& state)
        {
            // GF_GET_OUT_OF_JAIL original.
            //
            // Le paiement peut avoir provoqué une faillite.

            if (
                state.currentPlayer >=
                    state.numberOfPlayers ||
                state.players[
                    state.currentPlayer
                ].currentSquare >=
                    OffBoardSquare)
            {
                phases::pop(state);

                messaging::sendAction(
                    actions::Type::RestartPhase,
                    BankPlayer,
                    BankPlayer
                );

                return;
            }


            phases::switchTo(
                state,
                GamePhase::MovingToken,
                0,
                0,
                0
            );


            messaging::sendAction(
                actions::Type::JumpToSquare,
                BankPlayer,
                BankPlayer,
                JustVisitingSquare
            );
        }
    }


    void actionStartTurn(
        GameState& state,
        const actions::Message& message)
    {
        // ActionStartTurn().

        if (phases::current(state).phase !=
            GamePhase::WaitStartTurn)
        {
            sendWrongPhase(message);
            return;
        }

        if (message.fromPlayer != BankPlayer)
        {
            sendWrongPlayer(message);
            return;
        }

        if (!validCurrentPlayer(state))
        {
            // L'original appelle RULE_InitializeSystem() ici.
            //
            // Dans le runtime moderne, le reset est sérialisé dans
            // MESS afin de ne pas réinitialiser l'état au milieu
            // d'un dispatch de message.
            coreactions::resetFreshUnconditional(
                state
            );

            return;
        }

        state.numberOfDoublesRolled = 0;

        state.dice = { 0, 0 };
        state.utilityDice = { 0, 0 };

        state.justRolledOutOfJail = false;
        state.pendingCard = CardType::None;

        messaging::sendAction(
            actions::Type::NotifyStartTurn,
            BankPlayer,
            AllPlayers,
            state.currentPlayer
        );

        phases::switchTo(
            state,
            GamePhase::PreRoll,
            0,
            0,
            0
        );

        sendRestart();
    }


    void actionEndTurn(
        GameState& state,
        const actions::Message& message)
    {
        // Version finale du source :
        //
        // fromPlayer doit être CurrentPlayer,
        // et non RULE_BANK_PLAYER.

        if (message.fromPlayer !=
            state.currentPlayer)
        {
            sendWrongPlayer(message);
            return;
        }

        if (phases::current(state).phase !=
            GamePhase::WaitEndTurn)
        {
            sendWrongPhase(message);
            return;
        }

        PlayerState& current =
            state.players[state.currentPlayer];

        if (current.currentSquare ==
            InJailSquare)
        {
            ++current.turnsInJail;
        }


        // Trouver le joueur suivant non-bankrupt.
        PlayerNumber nextPlayer =
            state.currentPlayer;

        while (true)
        {
            ++nextPlayer;

            if (nextPlayer >=
                state.numberOfPlayers)
            {
                nextPlayer = 0;
            }

            if (nextPlayer ==
                state.currentPlayer)
            {
                break;
            }

            if (state.players[nextPlayer]
                    .currentSquare <
                OffBoardSquare)
            {
                break;
            }
        }

        state.currentPlayer = nextPlayer;


        // ActionEndTurn() original :
        //
        //  1. Nth bankruptcy
        //  2. time limit
        //  3. un seul survivant

        if (
            lifecycle::shouldEndGame(
                state
            ))
        {
            lifecycle::declareWinner(
                state
            );

            // Le source retourne AVANT
            // NOTIFY_ACTION_COMPLETED(ACTION_END_TURN).
            return;
        }


        sendActionCompleted(
            message,
            true
        );

        phases::switchTo(
            state,
            GamePhase::WaitStartTurn,
            0,
            0,
            0
        );


        // Source :
        //
        // si la queue est déjà vide, démarrage immédiat.
        // Sinon ACTION_TICK attend qu'elle se vide.
        if (messaging::currentQueueSize() == 0)
        {
            messaging::sendAction(
                actions::Type::StartTurn,
                BankPlayer,
                BankPlayer
            );
        }
    }


    void actionRollDice(
        GameState& state,
        const actions::Message& message)
    {
        const GamePhase phase =
            phases::current(state).phase;

        if (phase != GamePhase::WaitMoveRoll &&
            phase != GamePhase::WaitJailRoll &&
            phase != GamePhase::WaitUtilityRoll &&
            phase != GamePhase::PickingStartingOrder)
        {
            sendWrongPhase(message);
            return;
        }

        if (message.fromPlayer !=
            state.currentPlayer)
        {
            sendWrongPlayer(message);
            return;
        }

        if (!validCurrentPlayer(state))
        {
            return;
        }

        std::uint8_t dieA = 0;
        std::uint8_t dieB = 0;


        if (
            message.action ==
                actions::Type::CheatRollDice &&
            state.options.cheatingAllowed)
        {
            // ActionRollDice() original :
            // clamp chaque dé entre 1 et 6.

            dieA =
                static_cast<std::uint8_t>(
                    std::clamp<std::int64_t>(
                        message.numberA,
                        1,
                        6
                    )
                );


            dieB =
                static_cast<std::uint8_t>(
                    std::clamp<std::int64_t>(
                        message.numberB,
                        1,
                        6
                    )
                );


            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorPlayerCheating,
                0,
                message.fromPlayer
            );
        }
        else
        {
            // Même ACTION_CHEAT_ROLL_DICE avec cheatingAllowed
            // false tombe volontairement ici dans Rule.cpp :
            // il produit simplement un vrai lancer.

            generateDicePair(
                state,
                dieA,
                dieB
            );
        }

        const std::uint8_t total =
            static_cast<std::uint8_t>(
                dieA + dieB
            );

        const bool doubles =
            dieA == dieB;


        sendActionCompleted(
            message,
            true
        );

        messaging::sendAction(
            actions::Type::NotifyDiceRolled,
            BankPlayer,
            AllPlayers,
            dieA,
            dieB,
            state.currentPlayer
        );


        if (
            phase ==
                GamePhase::PickingStartingOrder)
        {
            startingorder::recordDiceRoll(
                state,
                dieA,
                dieB
            );

            return;
        }


        if (phase == GamePhase::WaitMoveRoll)
        {
            state.dice[0] = dieA;
            state.dice[1] = dieB;

            if (doubles)
            {
                ++state.numberOfDoublesRolled;
            }

            phases::switchTo(
                state,
                GamePhase::MovingToken,
                0,
                0,
                0
            );


            if (state.numberOfDoublesRolled >= 3)
            {
                messaging::sendAction(
                    actions::Type::JumpToSquare,
                    BankPlayer,
                    BankPlayer,
                    InJailSquare
                );

                return;
            }


            const PlayerState& player =
                state.players[state.currentPlayer];

            std::uint8_t newSquare =
                static_cast<std::uint8_t>(
                    player.currentSquare + total
                );

            if (newSquare > BoardwalkSquare)
            {
                newSquare =
                    static_cast<std::uint8_t>(
                        newSquare -
                        (BoardwalkSquare + 1)
                    );
            }

            messaging::sendAction(
                actions::Type::MoveForwards,
                BankPlayer,
                BankPlayer,
                newSquare
            );

            return;
        }


        // GF_WAIT_UTILITY_ROLL.
        if (phase == GamePhase::WaitUtilityRoll)
        {
            state.utilityDice[0] = dieA;
            state.utilityDice[1] = dieB;

            phases::pop(state);

            economy::collectRent(state);

            messaging::sendAction(
                actions::Type::RestartPhase,
                BankPlayer,
                BankPlayer
            );

            return;
        }


        // GF_WAIT_JAIL_ROLL.
        state.dice[0] = dieA;
        state.dice[1] = dieB;

        // Le source positionne ce flag même si le lancer
        // n'est finalement pas un double.
        state.justRolledOutOfJail = true;

        PlayerState& player =
            state.players[state.currentPlayer];

        if (doubles)
        {
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


        if (state.options.maximumTurnsInJail == 0 ||
            player.turnsInJail >=
                state.options.maximumTurnsInJail)
        {
            player.turnsInJail = 99;

            phases::switchTo(
                state,
                GamePhase::JailRollOrPayOrCardDecision,
                0,
                0,
                0
            );

            sendRestart();
            return;
        }


        popAndRestart(state);
    }


    void actionMoveForwards(
        GameState& state,
        const actions::Message& message)
    {
        if (message.fromPlayer != BankPlayer)
        {
            sendWrongPlayer(message);
            return;
        }

        if (phases::current(state).phase !=
            GamePhase::MovingToken)
        {
            sendWrongPhase(message);
            return;
        }

        if (!validCurrentPlayer(state))
        {
            return;
        }

        if (message.numberA < GoSquare ||
            message.numberA > BoardwalkSquare)
        {
            return;
        }

        PlayerState& player =
            state.players[state.currentPlayer];

        const std::uint8_t destination =
            static_cast<std::uint8_t>(
                message.numberA
            );

        const std::uint8_t previous =
            player.currentSquare;

        const std::uint8_t card =
            cardPreviewForSquare(
                state,
                destination
            );


        sendMoveNotification(
            actions::Type::NotifyMoveForwards,
            destination,
            previous,
            state.currentPlayer,
            card,
            !player.firstMoveMade
        );

        player.firstMoveMade = true;


        // Passage par GO :
        // destination <= previous.
        if (destination <= previous)
        {
            messaging::sendAction(
                actions::Type::NotifyPassedGo,
                BankPlayer,
                AllPlayers,
                state.currentPlayer
            );

            std::int64_t salary =
                state.options.passingGoAmount;

            if (state.options.doubleSalaryOnGo &&
                destination == GoSquare)
            {
                salary *= 2;
            }

            givePassingGoMoney(
                state,
                state.currentPlayer,
                salary
            );
        }


        player.currentSquare =
            destination;

        messaging::sendAction(
            actions::Type::LandedOnSquare,
            BankPlayer,
            BankPlayer
        );
    }


    void actionMoveBackwards(
        GameState& state,
        const actions::Message& message)
    {
        if (message.fromPlayer != BankPlayer)
        {
            sendWrongPlayer(message);
            return;
        }

        if (phases::current(state).phase !=
            GamePhase::MovingToken)
        {
            sendWrongPhase(message);
            return;
        }

        if (!validCurrentPlayer(state))
        {
            return;
        }

        if (message.numberA < GoSquare ||
            message.numberA > BoardwalkSquare)
        {
            return;
        }

        PlayerState& player =
            state.players[state.currentPlayer];

        const std::uint8_t destination =
            static_cast<std::uint8_t>(
                message.numberA
            );

        const std::uint8_t previous =
            player.currentSquare;

        sendMoveNotification(
            actions::Type::NotifyMoveBackwards,
            destination,
            previous,
            state.currentPlayer,
            cardPreviewForSquare(
                state,
                destination
            ),
            false
        );

        player.currentSquare =
            destination;

        messaging::sendAction(
            actions::Type::LandedOnSquare,
            BankPlayer,
            BankPlayer
        );
    }


    void actionJumpToSquare(
        GameState& state,
        const actions::Message& message)
    {
        if (message.fromPlayer != BankPlayer)
        {
            sendWrongPlayer(message);
            return;
        }

        if (phases::current(state).phase !=
            GamePhase::MovingToken)
        {
            sendWrongPhase(message);
            return;
        }

        if (!validCurrentPlayer(state))
        {
            return;
        }

        if (message.numberA < GoSquare ||
            message.numberA > InJailSquare)
        {
            return;
        }

        PlayerState& player =
            state.players[state.currentPlayer];

        const std::uint8_t destination =
            static_cast<std::uint8_t>(
                message.numberA
            );

        messaging::sendAction(
            actions::Type::NotifyJumpToSquare,
            BankPlayer,
            AllPlayers,
            destination,
            player.currentSquare,
            state.currentPlayer,
            0
        );


        if (destination == GoSquare)
        {
            messaging::sendAction(
                actions::Type::NotifyPassedGo,
                BankPlayer,
                AllPlayers,
                state.currentPlayer
            );

            std::int64_t salary =
                state.options.passingGoAmount;

            if (state.options.doubleSalaryOnGo)
            {
                salary *= 2;
            }

            givePassingGoMoney(
                state,
                state.currentPlayer,
                salary
            );
        }


        player.currentSquare =
            destination;

        messaging::sendAction(
            actions::Type::LandedOnSquare,
            BankPlayer,
            BankPlayer
        );
    }


    void actionLandedOnSquare(
        GameState& state,
        const actions::Message& message)
    {
        if (message.fromPlayer != BankPlayer)
        {
            sendWrongPlayer(message);
            return;
        }

        if (phases::current(state).phase !=
            GamePhase::MovingToken)
        {
            sendWrongPhase(message);
            return;
        }

        if (!validCurrentPlayer(state))
        {
            return;
        }

        PlayerState& player =
            state.players[state.currentPlayer];

        const auto square =
            static_cast<board::SquareType>(
                player.currentSquare
            );


        switch (square)
        {
            case board::SquareType::Chance1:
            case board::SquareType::Chance2:
            case board::SquareType::Chance3:
            case board::SquareType::CommunityChest1:
            case board::SquareType::CommunityChest2:
            case board::SquareType::CommunityChest3:
            {
                phases::switchTo(
                    state,
                    GamePhase::WaitUntilCardSeen,
                    0,
                    0,
                    0
                );

                sendRestart();
                return;
            }


            case board::SquareType::JustVisiting:
            {
                if (state.justRolledOutOfJail)
                {
                    messaging::sendAction(
                        actions::Type::MoveForwards,
                        BankPlayer,
                        BankPlayer,
                        JustVisitingSquare +
                            state.dice[0] +
                            state.dice[1]
                    );
                }
                else
                {
                    popAndRestart(state);
                }

                return;
            }


            case board::SquareType::InJail:
            {
                player.turnsInJail = 0;

                popAndRestart(state);
                return;
            }


            case board::SquareType::GoToJail:
            {
                messaging::sendAction(
                    actions::Type::JumpToSquare,
                    BankPlayer,
                    BankPlayer,
                    InJailSquare
                );

                return;
            }


            case board::SquareType::Go:
            {
                popAndRestart(state);
                return;
            }


            case board::SquareType::IncomeTax:
            {
                economy::landOnIncomeTax(state);
                return;
            }


            case board::SquareType::FreeParking:
            {
                economy::landOnFreeParking(state);
                return;
            }


            case board::SquareType::LuxuryTax:
            {
                economy::landOnLuxuryTax(state);
                return;
            }


            default:
                break;
        }


        if (board::isOwnable(square))
        {
            const PlayerNumber owner =
                state.squares[
                    static_cast<std::size_t>(
                        square
                    )
                ].owner;


            if (owner < MaxPlayers)
            {
                // Source :
                //
                // PopPhase();
                // CollectRent();
                // ACTION_RESTART_PHASE

                phases::pop(state);

                economy::collectRent(state);

                messaging::sendAction(
                    actions::Type::RestartPhase,
                    BankPlayer,
                    BankPlayer
                );

                return;
            }


            if (owner == NobodyPlayer)
            {
                phases::switchTo(
                    state,
                    GamePhase::AuctionOrBuyDecision,
                    0,
                    0,
                    0
                );

                messaging::sendAction(
                    actions::Type::RestartPhase,
                    BankPlayer,
                    BankPlayer
                );

                return;
            }


            // BANK / ESCROW :
            // le source considère cela comme un état
            // transitoire et ignore la case.

            popAndRestart(state);
            return;
        }


        // Case sans effet particulier.
        popAndRestart(state);
    }


    bool restartGameplayPhase(
        GameState& state,
        const actions::Message& message)
    {
        switch (phases::current(state).phase)
        {
            case GamePhase::PickingStartingOrder:
                startingorder::restart(state);
                return true;

            case GamePhase::WaitStartTurn:
            case GamePhase::MovingToken:
                // Le source ne fait volontairement rien lors
                // d'un restart de ces phases.
                return true;


            case GamePhase::WaitEndTurn:
                restartWaitEndTurn(state);
                return true;


            case GamePhase::PreRoll:
                if (
                    message.fromPlayer !=
                    BankPlayer)
                {
                    return true;
                }

                restartPreRoll(state);
                return true;


            case GamePhase::WaitMoveRoll:
            case GamePhase::WaitUtilityRoll:
                restartWaitMoveRoll(state);
                return true;


            case GamePhase::WaitUntilCardSeen:
                restartWaitUntilCardSeen(state);
                return true;


            case GamePhase::AuctionOrBuyDecision:
                restartBuyOrAuction(state);
                return true;


            case GamePhase::JailRollOrPayOrCardDecision:
                restartJailDecision(state);
                return true;


            case GamePhase::GetOutOfJail:
                restartGetOutOfJail(state);
                return true;


            case GamePhase::FlatOrFractionTaxDecision:
            {
                if (!validCurrentPlayer(state))
                {
                    popAndRestart(state);
                    return true;
                }


                prediction::sendNextMove(
                    state
                );


                messaging::sendAction(
                    actions::Type::NotifyFlatOrFractionTaxDecision,
                    BankPlayer,
                    AllPlayers,
                    state.currentPlayer,
                    state.options.flatTaxFee,
                    state.options.taxRate
                );

                return true;
            }


            default:
                return false;
        }
    }


    void onIdleTick(
        GameState& state)
    {
        // ActionTick() original :
        //
        // GF_WAIT_START_TURN attend que la queue soit vide
        // avant de lancer ACTION_START_TURN.

        if (state.numberOfPendingPhases == 0)
        {
            return;
        }

        if (phases::current(state).phase !=
            GamePhase::WaitStartTurn)
        {
            return;
        }

        if (messaging::currentQueueSize() != 0)
        {
            return;
        }

        messaging::sendAction(
            actions::Type::StartTurn,
            BankPlayer,
            BankPlayer
        );
    }
}







