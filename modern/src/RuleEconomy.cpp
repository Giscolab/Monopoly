#include "RuleEconomy.hpp"

#include "BoardRules.hpp"
#include "LegacyTextIds.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"
#include "RuleLifecycle.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace monopoly::rules::economy
{
    namespace
    {
        constexpr std::uint8_t BoardwalkSquare = 39;
        constexpr std::uint8_t InJailSquare = 40;
        constexpr std::uint8_t OffBoardSquare = 41;


        std::optional<GameState> debtSnapshot;
        bool debtStateChanged = false;


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


        void notifyActionCompleted(
            const actions::Message& message,
            bool success,
            std::int64_t result = 0)
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
                result
            );
        }


        void notifyCashChange(
            GameState& state,
            PlayerNumber player,
            std::int64_t delta)
        {
            if (player >= state.numberOfPlayers)
            {
                return;
            }

            messaging::sendAction(
                actions::Type::NotifyCashAmount,
                BankPlayer,
                AllPlayers,
                player,
                delta,
                state.players[player].cash
            );
        }


        std::int64_t cashPlayerCouldRaiseIfNeeded(
            const GameState& state,
            PlayerNumber player)
        {
            // CashPlayerCouldRaiseIfNeeded() original.
            //
            // N'inclut pas :
            //   - les trades
            //   - les propriétés en escrow
            //
            // Inclut :
            //   - cash
            //   - hypothèque possible
            //   - vente des bâtiments.

            if (player >= state.numberOfPlayers)
            {
                return 0;
            }

            if (state.players[player].currentSquare >=
                OffBoardSquare)
            {
                return 0;
            }

            std::int64_t cash =
                state.players[player].cash;

            for (std::size_t squareNo = 0;
                 squareNo < SquareCount;
                 ++squareNo)
            {
                const SquareState& square =
                    state.squares[squareNo];

                if (square.owner != player)
                {
                    continue;
                }

                const board::SquareDefinition& predefined =
                    board::definition(
                        static_cast<board::SquareType>(
                            squareNo
                        )
                    );

                if (!square.mortgaged)
                {
                    cash += predefined.mortgageCost;
                }

                cash +=
                    static_cast<std::int64_t>(
                        square.houses
                    ) *
                    ((predefined.housePurchaseCost + 1) / 2);
            }

            return cash;
        }


        void transferProperty(
            GameState& state,
            board::SquareType squareType,
            PlayerNumber toPlayer)
        {
            // TransferProperty() original.

            const std::size_t squareNo =
                static_cast<std::size_t>(
                    squareType
                );

            SquareState& square =
                state.squares[squareNo];

            const PlayerNumber previousOwner =
                square.owner;


            const bool targetIsBankOrInvalid =
                toPlayer >= state.numberOfPlayers ||
                (
                    toPlayer < MaxPlayers &&
                    state.players[toPlayer].currentSquare >=
                        OffBoardSquare
                );


            if (targetIsBankOrInvalid)
            {
                messaging::sendAction(
                    actions::Type::NotifySquareOwnership,
                    BankPlayer,
                    AllPlayers,
                    squareNo,
                    BankPlayer,
                    previousOwner
                );

                square.owner = BankPlayer;
                square.offeredInTradeTo =
                    NobodyPlayer;


                // La banque dés-hypothèque automatiquement.
                if (square.mortgaged)
                {
                    messaging::sendAction(
                        actions::Type::NotifySquareMortgage,
                        BankPlayer,
                        AllPlayers,
                        squareNo,
                        0,
                        1
                    );

                    square.mortgaged = false;
                }

                return;
            }


            // Transfert à un joueur :
            // propriété placée temporairement dans l'escrow.

            messaging::sendAction(
                actions::Type::NotifySquareOwnership,
                BankPlayer,
                AllPlayers,
                squareNo,
                EscrowPlayer,
                previousOwner
            );

            square.owner =
                EscrowPlayer;

            square.offeredInTradeTo =
                toPlayer;
        }


        void sellAllBuildingsOnMonopoly(
            GameState& state,
            board::SquareType referenceSquare)
        {
            // SellAllBuildingsOnMonopoly() original.

            const auto group =
                board::definition(
                    referenceSquare
                ).group;

            const std::size_t referenceNo =
                static_cast<std::size_t>(
                    referenceSquare
                );

            const PlayerNumber player =
                state.squares[
                    referenceNo
                ].owner;

            std::int64_t cash = 0;


            for (std::size_t squareNo = 0;
                 squareNo < SquareCount;
                 ++squareNo)
            {
                if (board::definition(
                        static_cast<board::SquareType>(
                            squareNo
                        )
                    ).group != group)
                {
                    continue;
                }

                SquareState& square =
                    state.squares[squareNo];

                const std::uint8_t houses =
                    square.houses;

                square.houses = 0;

                if (houses == 0)
                {
                    continue;
                }


                messaging::sendAction(
                    actions::Type::NotifySquareHouses,
                    BankPlayer,
                    AllPlayers,
                    squareNo,
                    0,
                    state.options.housesPerHotel
                );


                cash +=
                    (
                        static_cast<std::int64_t>(
                            houses
                        ) *
                        board::definition(
                            static_cast<board::SquareType>(
                                squareNo
                            )
                        ).housePurchaseCost
                        + 1
                    ) / 2;
            }


            if (cash <= 0)
            {
                return;
            }


            blindlyTransferCash(
                state,
                BankPlayer,
                player,
                cash,
                true
            );


            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorBuildingAllSold,
                player,
                cash,
                referenceNo
            );
        }


        void returnCardToBottom(
            CardDeck& deck,
            CardType card)
        {
            if (deck.cardCount >=
                MaxCardsInDeck)
            {
                return;
            }

            deck.cardPile[
                deck.cardCount++
            ] =
                static_cast<std::uint8_t>(
                    card
                );
        }


        void transferGetOutOfJail(
            GameState& state,
            PlayerNumber fromPlayer,
            PlayerNumber toPlayer,
            DeckType deckType)
        {
            CardDeck& deck =
                state.cards[
                    static_cast<std::size_t>(
                        deckType
                    )
                ];

            PlayerNumber normalizedFrom =
                fromPlayer;

            PlayerNumber normalizedTo =
                toPlayer;


            if (normalizedFrom >= MaxPlayers)
            {
                normalizedFrom =
                    NobodyPlayer;
            }

            if (normalizedTo >= MaxPlayers)
            {
                normalizedTo =
                    NobodyPlayer;
            }


            if (deck.jailOwner !=
                normalizedFrom)
            {
                return;
            }


            if (deck.jailOwner < MaxPlayers &&
                normalizedTo ==
                    NobodyPlayer)
            {
                const CardType jailCard =
                    deckType == DeckType::Chance
                        ? CardType::ChanceGetOutOfJailFree
                        : CardType::CommunityGetOutOfJailFree;

                returnCardToBottom(
                    deck,
                    jailCard
                );
            }


            deck.jailOwner =
                normalizedTo;

            deck.jailOfferedInTradeTo =
                NobodyPlayer;


            messaging::sendAction(
                actions::Type::NotifyJailCardOwnership,
                BankPlayer,
                AllPlayers,
                deck.jailOwner,
                static_cast<std::int64_t>(
                    deckType
                )
            );
        }


        bool quickMortgagePhaseAllowed(
            GamePhase phase)
        {
            switch (phase)
            {
                case GamePhase::WaitMoveRoll:
                case GamePhase::WaitEndTurn:
                case GamePhase::WaitJailRoll:
                case GamePhase::WaitUtilityRoll:
                case GamePhase::WaitUntilCardSeen:
                case GamePhase::JailRollOrPayOrCardDecision:
                case GamePhase::FlatOrFractionTaxDecision:
                case GamePhase::AuctionOrBuyDecision:
                case GamePhase::CollectingPayment:
                case GamePhase::FreeUnmortgage:
                case GamePhase::BuySellMortgage:
                    return true;

                default:
                    return false;
            }
        }


        bool quickSellPhaseAllowed(
            GamePhase phase)
        {
            switch (phase)
            {
                case GamePhase::WaitMoveRoll:
                case GamePhase::WaitEndTurn:
                case GamePhase::WaitJailRoll:
                case GamePhase::WaitUtilityRoll:
                case GamePhase::WaitUntilCardSeen:
                case GamePhase::JailRollOrPayOrCardDecision:
                case GamePhase::FlatOrFractionTaxDecision:
                case GamePhase::AuctionOrBuyDecision:
                case GamePhase::CollectingPayment:
                case GamePhase::FreeUnmortgage:
                case GamePhase::BuySellMortgage:
                case GamePhase::DecomposeHotel:
                    return true;

                default:
                    return false;
            }
        }


        void restartCollectingPayment(
            GameState& state)
        {
            const PendingPhase debt =
                phases::current(state);


            if (debt.toPlayer < MaxPlayers &&
                (
                    debt.toPlayer >=
                        state.numberOfPlayers ||
                    state.players[
                        debt.toPlayer
                    ].currentSquare >=
                        OffBoardSquare
                ))
            {
                messaging::sendAction(
                    actions::Type::NotifyErrorMessage,
                    BankPlayer,
                    AllPlayers,
                    legacy_text::ErrorCollectorBankrupt,
                    debt.fromPlayer,
                    debt.toPlayer,
                    debt.amount
                );

                debtSnapshot.reset();
                debtStateChanged = false;

                popAndRestart(state);
                return;
            }


            if (debt.fromPlayer < MaxPlayers &&
                (
                    debt.fromPlayer >=
                        state.numberOfPlayers ||
                    state.players[
                        debt.fromPlayer
                    ].currentSquare >=
                        OffBoardSquare
                ))
            {
                messaging::sendAction(
                    actions::Type::NotifyErrorMessage,
                    BankPlayer,
                    AllPlayers,
                    legacy_text::ErrorDebtorBankrupt,
                    debt.fromPlayer,
                    debt.toPlayer,
                    debt.amount
                );

                debtSnapshot.reset();
                debtStateChanged = false;

                popAndRestart(state);
                return;
            }


            const bool bankPays =
                debt.fromPlayer >=
                    MaxPlayers;

            const bool playerCanPay =
                !bankPays &&
                state.players[
                    debt.fromPlayer
                ].cash >=
                    debt.amount;


            if (bankPays || playerCanPay)
            {
                blindlyTransferCash(
                    state,
                    debt.fromPlayer,
                    debt.toPlayer,
                    debt.amount,
                    true
                );


                messaging::sendAction(
                    actions::Type::NotifyActionCompleted,
                    BankPlayer,
                    debt.fromPlayer,
                    static_cast<std::int64_t>(
                        actions::Type::NotifyPleasePay
                    ),
                    1,
                    debt.fromPlayer
                );


                debtSnapshot.reset();
                debtStateChanged = false;

                popAndRestart(state);
                return;
            }


            // Premier passage dans une dette impossible
            // à payer immédiatement :
            //
            // SaveGameStateInCurrentPhase() original.

            if (!debtSnapshot.has_value())
            {
                debtSnapshot = state;
                debtStateChanged = false;
            }


            actions::Message paymentRequest{};

            paymentRequest.action =
                actions::Type::NotifyPleasePay;

            paymentRequest.fromPlayer =
                BankPlayer;

            paymentRequest.toPlayer =
                AllPlayers;

            paymentRequest.numberA =
                debt.fromPlayer;

            paymentRequest.numberB =
                debt.toPlayer;

            paymentRequest.numberC =
                debt.amount;

            paymentRequest.numberD =
                debt.amount -
                state.players[
                    debt.fromPlayer
                ].cash;

            paymentRequest.numberE =
                cashPlayerCouldRaiseIfNeeded(
                    state,
                    debt.fromPlayer
                ) < debt.amount
                    ? 1
                    : 0;

            messaging::sendAction(
                paymentRequest
            );
        }


        void restartTransferEscrowProperty(
            GameState& state)
        {
            const PendingPhase phase =
                phases::current(state);

            const PlayerNumber target =
                phase.toPlayer;

            const std::uint32_t properties =
                static_cast<std::uint32_t>(
                    phase.amount
                );


            const bool bankrupt =
                target >=
                    state.numberOfPlayers ||
                state.players[
                    target
                ].currentSquare >=
                    OffBoardSquare;


            for (std::size_t squareNo = 0;
                 squareNo < SquareCount;
                 ++squareNo)
            {
                const board::SquareType squareType =
                    static_cast<board::SquareType>(
                        squareNo
                    );

                if (
                    (
                        board::propertyBit(
                            squareType
                        ) &
                        properties
                    ) == 0)
                {
                    continue;
                }


                SquareState& square =
                    state.squares[squareNo];


                if (square.owner !=
                        EscrowPlayer ||
                    square.offeredInTradeTo !=
                        target)
                {
                    continue;
                }


                square.owner =
                    bankrupt
                        ? BankPlayer
                        : target;

                square.offeredInTradeTo =
                    NobodyPlayer;


                messaging::sendAction(
                    actions::Type::NotifySquareOwnership,
                    BankPlayer,
                    AllPlayers,
                    squareNo,
                    square.owner,
                    EscrowPlayer
                );
            }


            // Original :
            // GF_TRANSFER_ESCROW_PROPERTY
            //     ↓
            // GF_FREE_UNMORTGAGE

            phases::switchTo(
                state,
                GamePhase::FreeUnmortgage,
                0,
                target,
                properties
            );

            sendRestart();
        }


        void restartFreeUnmortgage(
            GameState& state)
        {
            PendingPhase& phase =
                state.phaseStack[0];

            const PlayerNumber target =
                phase.toPlayer;


            if (target >=
                    state.numberOfPlayers ||
                state.players[
                    target
                ].currentSquare >=
                    OffBoardSquare)
            {
                popAndRestart(state);
                return;
            }


            std::uint32_t stillMortgaged = 0;


            for (std::size_t squareNo = 0;
                 squareNo < SquareCount;
                 ++squareNo)
            {
                const board::SquareType squareType =
                    static_cast<board::SquareType>(
                        squareNo
                    );

                const std::uint32_t bit =
                    board::propertyBit(
                        squareType
                    );


                if (
                    (
                        bit &
                        static_cast<std::uint32_t>(
                            phase.amount
                        )
                    ) == 0)
                {
                    continue;
                }


                const SquareState& square =
                    state.squares[squareNo];


                if (square.owner == target &&
                    square.mortgaged)
                {
                    stillMortgaged |= bit;
                }
            }


            phase.amount =
                static_cast<std::int64_t>(
                    stillMortgaged
                );


            if (stillMortgaged != 0)
            {
                messaging::sendAction(
                    actions::Type::NotifyFreeUnmortgaging,
                    BankPlayer,
                    AllPlayers,
                    target,
                    stillMortgaged
                );

                return;
            }


            popAndRestart(state);
        }
    }


    void sellAllBuildingsForSettlement(
        GameState& state,
        std::uint8_t squareNo)
    {
        if (squareNo >= SquareCount)
        {
            return;
        }

        sellAllBuildingsOnMonopoly(
            state,
            static_cast<board::SquareType>(
                squareNo
            )
        );
    }


    void transferPropertyForSettlement(
        GameState& state,
        std::uint8_t squareNo,
        PlayerNumber toPlayer)
    {
        transferProperty(
            state,
            static_cast<board::SquareType>(
                squareNo
            ),
            toPlayer
        );
    }


    void resetTransientState()
    {
        debtSnapshot.reset();
        debtStateChanged = false;
    }


    void blindlyTransferCash(
        GameState& state,
        PlayerNumber fromPlayer,
        PlayerNumber toPlayer,
        std::int64_t amount,
        bool sendAnimation)
    {
        // BlindlyTransferCash() original.

        if (amount <= 0)
        {
            return;
        }


        if (fromPlayer <
            state.numberOfPlayers)
        {
            state.players[
                fromPlayer
            ].cash -= amount;
        }


        if (toPlayer <
            state.numberOfPlayers)
        {
            state.players[
                toPlayer
            ].cash += amount;
        }


        if (sendAnimation)
        {
            messaging::sendAction(
                actions::Type::NotifyCashAnimation,
                BankPlayer,
                AllPlayers,
                fromPlayer,
                toPlayer,
                amount
            );
        }


        // Comme l'original :
        // notifications APRES l'animation.

        if (fromPlayer <
            state.numberOfPlayers)
        {
            notifyCashChange(
                state,
                fromPlayer,
                amount
            );
        }


        if (toPlayer <
            state.numberOfPlayers)
        {
            notifyCashChange(
                state,
                toPlayer,
                amount
            );
        }
    }


    void addMoneyToFreeParkingPot(
        GameState& state,
        std::int64_t amount)
    {
        // AddMoneyToFreeParkingPot().

        state.freeParkingJackpotAmount +=
            amount;


        if (state.options.freeParkingPot)
        {
            messaging::sendAction(
                actions::Type::NotifyFreeParkingPot,
                BankPlayer,
                AllPlayers,
                state.freeParkingJackpotAmount,
                amount
            );
        }
    }


    void stackDebt(
        GameState& state,
        PlayerNumber fromPlayer,
        PlayerNumber toPlayer,
        std::int64_t amount)
    {
        // StackDebt().

        phases::push(
            state,
            GamePhase::CollectingPayment,
            fromPlayer,
            toPlayer,
            amount
        );
    }


    void stackDebtAndRestart(
        GameState& state,
        PlayerNumber fromPlayer,
        PlayerNumber toPlayer,
        std::int64_t amount)
    {
        stackDebt(
            state,
            fromPlayer,
            toPlayer,
            amount
        );

        sendRestart();
    }


    void collectRent(GameState& state)
    {
        // CollectRent() original, maintenant avec
        // futures et immunités actifs.

        const PlayerNumber landingPlayer =
            state.currentPlayer;


        if (landingPlayer >=
            state.numberOfPlayers)
        {
            return;
        }


        const std::uint8_t squareNo =
            state.players[
                landingPlayer
            ].currentSquare;


        if (squareNo >= SquareCount)
        {
            return;
        }


        SquareState& square =
            state.squares[squareNo];


        const board::SquareType squareType =
            static_cast<board::SquareType>(
                squareNo
            );


        const board::SquareDefinition& predefined =
            board::definition(
                squareType
            );


        const PlayerNumber owner =
            square.owner;


        const std::uint32_t squareBit =
            board::propertyBit(
                squareType
            );


        std::int64_t rent = 0;

        bool needUtilityRoll = false;


        // ----------------------------------------------------
        // Calcul normal du loyer.
        // ----------------------------------------------------

        if (!square.mortgaged)
        {
            int squaresOwnedInGroup = 0;
            int totalSquaresInGroup = 0;


            for (std::size_t testNo = 0;
                 testNo < SquareCount;
                 ++testNo)
            {
                if (
                    board::definition(
                        static_cast<board::SquareType>(
                            testNo
                        )
                    ).group !=
                    predefined.group)
                {
                    continue;
                }


                ++totalSquaresInGroup;


                const SquareState& testSquare =
                    state.squares[testNo];


                if (
                    testSquare.owner == owner &&
                    (
                        !testSquare.mortgaged ||
                        state.options
                            .mortgagedCountsInGroupRent
                    ))
                {
                    ++squaresOwnedInGroup;
                }
            }


            // Terrains.

            if (
                static_cast<std::uint8_t>(
                    predefined.group
                ) <=
                static_cast<std::uint8_t>(
                    board::SquareGroup::ParkPlace
                ))
            {
                const std::size_t houseIndex =
                    std::min<std::size_t>(
                        square.houses,
                        board::RentStepCount - 1
                    );


                rent =
                    predefined.rent[
                        houseIndex
                    ];


                if (
                    square.houses == 0 &&
                    squaresOwnedInGroup ==
                        totalSquaresInGroup)
                {
                    rent += rent;
                }
            }


            // Gares.

            if (
                predefined.group ==
                board::SquareGroup::Railroad)
            {
                const std::size_t index =
                    std::min<std::size_t>(
                        squaresOwnedInGroup,
                        board::RentStepCount - 1
                    );


                rent =
                    predefined.rent[index];


                if (
                    state.pendingCard ==
                        CardType::
                            ChanceGoToNearestRailroadPayDouble1 ||
                    state.pendingCard ==
                        CardType::
                            ChanceGoToNearestRailroadPayDouble2)
                {
                    rent += rent;

                    state.pendingCard =
                        CardType::None;
                }
            }


            // Utilities.

            if (
                predefined.group ==
                board::SquareGroup::Utility)
            {
                if (
                    state.pendingCard ==
                    CardType::
                        ChanceGoToNearestUtility)
                {
                    if (
                        state.utilityDice[0] == 0)
                    {
                        needUtilityRoll = true;

                        // Sentinel exact du source.
                        rent = 12345;
                    }
                    else
                    {
                        state.pendingCard =
                            CardType::None;


                        rent =
                            state.utilityDice[0] +
                            state.utilityDice[1];


                        rent *= 10;


                        state.utilityDice =
                            { 0, 0 };
                    }
                }
                else
                {
                    rent =
                        state.dice[0] +
                        state.dice[1];


                    rent *=
                        squaresOwnedInGroup <= 1
                            ? 4
                            : 10;
                }
            }
        }


        // ----------------------------------------------------
        // Chercher un FUTURE RENT.
        //
        // Un hit compte même si la propriété est hypothéquée.
        // ----------------------------------------------------

        CountHitRecord* future = nullptr;


        for (CountHitRecord& hit :
             state.countHits)
        {
            if (
                hit.toPlayer != NobodyPlayer &&
                hit.hitType ==
                    CountHitType::FutureRent &&
                (
                    hit.properties &
                    squareBit
                ) != 0)
            {
                future = &hit;
                break;
            }
        }


        const PlayerNumber rentRecipient =
            future != nullptr
                ? future->toPlayer
                : owner;


        // ----------------------------------------------------
        // Immunité du joueur qui vient d'atterrir.
        // ----------------------------------------------------

        CountHitRecord* immunity = nullptr;


        for (CountHitRecord& hit :
             state.countHits)
        {
            if (
                hit.toPlayer ==
                    landingPlayer &&
                hit.hitType ==
                    CountHitType::RentImmunity &&
                (
                    hit.properties &
                    squareBit
                ) != 0)
            {
                immunity = &hit;
                break;
            }
        }


        // Pas de loyer à soi-même.
        //
        // L'original n'utilise PAS non plus une immunité
        // lorsqu'elle serait inutile.

        if (
            landingPlayer ==
            rentRecipient)
        {
            rent = 0;
            immunity = nullptr;
        }


        if (immunity != nullptr)
        {
            rent = 0;
        }


        // ----------------------------------------------------
        // Utility Chance :
        //
        // si une immunité annule le loyer, inutile de lancer.
        // ----------------------------------------------------

        if (needUtilityRoll)
        {
            if (rent > 0)
            {
                phases::push(
                    state,
                    GamePhase::WaitUtilityRoll,
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


            state.pendingCard =
                CardType::None;
        }


        // ----------------------------------------------------
        // Dette de loyer.
        // ----------------------------------------------------

        if (rent > 0)
        {
            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorRentInfo,
                rent,
                rentRecipient,
                squareNo
            );


            stackDebt(
                state,
                landingPlayer,
                rentRecipient,
                rent
            );


            square.gameEarnings +=
                rent;
        }


        // ----------------------------------------------------
        // UseUpOneHit().
        // ----------------------------------------------------

        auto useHit =
            [&state, squareNo](
                CountHitRecord* hit)
            {
                if (hit == nullptr)
                {
                    return;
                }


                int count =
                    hit->hitCount - 1;


                if (count < 0)
                {
                    count = 0;
                }


                hit->hitCount =
                    count;


                actions::Message notification{};

                notification.action =
                    hit->hitType ==
                        CountHitType::RentImmunity
                        ? actions::Type::
                            NotifyImmunityCount
                        : actions::Type::
                            NotifyFutureRentCount;

                notification.fromPlayer =
                    BankPlayer;

                notification.toPlayer =
                    AllPlayers;

                notification.numberA =
                    hit->toPlayer;

                notification.numberB =
                    count;

                notification.numberC =
                    squareNo;

                notification.numberD =
                    hit->fromPlayer;

                notification.numberE =
                    hit->properties;


                messaging::sendAction(
                    notification
                );


                if (count <= 0)
                {
                    hit->toPlayer =
                        NobodyPlayer;
                }
            };


        // ----------------------------------------------------
        // FUTURE :
        // consommé à CHAQUE arrivée sur la case.
        // ----------------------------------------------------

        if (future != nullptr)
        {
            const PlayerNumber futurePlayer =
                rentRecipient;

            const PlayerNumber futureOwner =
                owner;


            useHit(future);


            actions::Message info{};

            info.action =
                actions::Type::NotifyErrorMessage;

            info.fromPlayer =
                BankPlayer;

            info.toPlayer =
                AllPlayers;

            info.numberA =
                legacy_text::ErrorFutureRentUsed;

            info.numberB =
                futurePlayer;

            info.numberC =
                futureOwner;

            info.numberD =
                squareNo;

            info.numberE =
                future->hitCount;


            messaging::sendAction(info);
        }


        // ----------------------------------------------------
        // IMMUNITÉ.
        // ----------------------------------------------------

        if (immunity != nullptr)
        {
            const PlayerNumber immunityFrom =
                rentRecipient;


            useHit(immunity);


            actions::Message info{};

            info.action =
                actions::Type::NotifyErrorMessage;

            info.fromPlayer =
                BankPlayer;

            info.toPlayer =
                AllPlayers;

            info.numberA =
                legacy_text::ErrorImmunityUsed;

            info.numberB =
                landingPlayer;

            info.numberC =
                immunityFrom;

            info.numberD =
                squareNo;

            info.numberE =
                immunity->hitCount;


            messaging::sendAction(info);
        }
    }

    void actionBuyOrAuctionDecision(
        GameState& state,
        const actions::Message& message)
    {
        // ActionBuyOrAuctionDecision() original.

        if (
            phases::current(state).phase !=
            GamePhase::AuctionOrBuyDecision)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        if (
            message.fromPlayer !=
            state.currentPlayer)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        notifyActionCompleted(
            message,
            true,
            message.numberA
        );


        PlayerState& player =
            state.players[
                state.currentPlayer
            ];


        const auto squareType =
            static_cast<board::SquareType>(
                player.currentSquare
            );


        SquareState& square =
            state.squares[
                player.currentSquare
            ];


        if (message.numberA != 0)
        {
            // Achat :
            //
            //   propriété → ESCROW
            //       ↓
            //   GF_TRANSFER_ESCROW_PROPERTY
            //       ↓
            //   GF_COLLECTING_PAYMENT
            //
            // Le joueur ne peut donc pas hypothéquer
            // immédiatement la propriété qu'il achète
            // pour payer son propre achat.

            transferProperty(
                state,
                squareType,
                state.currentPlayer
            );


            phases::switchTo(
                state,
                GamePhase::TransferEscrowProperty,
                0,
                state.currentPlayer,
                board::propertyBit(
                    squareType
                )
            );


            stackDebtAndRestart(
                state,
                state.currentPlayer,
                BankPlayer,
                board::definition(
                    squareType
                ).purchaseCost
            );

            return;
        }


        // Refus :
        // la banque la marque pour enchère ultérieure.

        square.owner =
            BankPlayer;

        popAndRestart(state);
    }


    void actionTaxDecision(
        GameState& state,
        const actions::Message& message)
    {
        // ActionTaxDecision() original.

        const PlayerNumber player =
            message.fromPlayer;


        if (player !=
            state.currentPlayer)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        if (
            phases::current(state).phase !=
            GamePhase::FlatOrFractionTaxDecision)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        std::int64_t taxFee = 0;


        if (message.numberA != 0)
        {
            // Impôt proportionnel :
            //
            // valeur d'achat pleine des propriétés
            // même hypothéquées
            // + prix d'achat des bâtiments
            // + cash.

            for (std::size_t squareNo = 0;
                 squareNo < SquareCount;
                 ++squareNo)
            {
                if (
                    state.squares[
                        squareNo
                    ].owner != player)
                {
                    continue;
                }


                const board::SquareDefinition& predefined =
                    board::definition(
                        static_cast<board::SquareType>(
                            squareNo
                        )
                    );


                taxFee +=
                    predefined.purchaseCost;


                taxFee +=
                    predefined.housePurchaseCost *
                    state.squares[
                        squareNo
                    ].houses;
            }


            taxFee +=
                state.players[
                    player
                ].cash;


            taxFee =
                (
                    taxFee *
                    state.options.taxRate +
                    50
                ) / 100;
        }
        else
        {
            taxFee =
                state.options.flatTaxFee;
        }


        notifyActionCompleted(
            message,
            true,
            message.numberA
        );


        phases::pop(state);


        stackDebtAndRestart(
            state,
            player,
            BankPlayer,
            taxFee
        );


        addMoneyToFreeParkingPot(
            state,
            taxFee
        );


        messaging::sendAction(
            actions::Type::NotifyErrorMessage,
            BankPlayer,
            AllPlayers,
            legacy_text::ErrorTaxesCharged,
            taxFee,
            player
        );
    }


    void actionFreeUnmortgageDone(
        GameState& state,
        const actions::Message& message)
    {
        // ActionFreeUnmortgageDone() original.

        if (state.numberOfPendingPhases == 0)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        if (
            message.fromPlayer !=
            phases::current(state).toPlayer)
        {
            notifyActionCompleted(
                message,
                false
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
                static_cast<std::int64_t>(
                    phases::current(state).phase
                )
            );

            return;
        }


        if (
            phases::current(state).phase !=
            GamePhase::FreeUnmortgage)
        {
            notifyActionCompleted(
                message,
                false
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
                static_cast<std::int64_t>(
                    phases::current(state).phase
                )
            );

            return;
        }


        notifyActionCompleted(
            message,
            true
        );


        phases::pop(state);


        messaging::sendAction(
            actions::Type::RestartPhase,
            BankPlayer,
            BankPlayer
        );
    }


    void actionMortgaging(
        GameState& state,
        const actions::Message& message)
    {
        // ActionMortgaging() original.

        const PlayerNumber player =
            message.fromPlayer;


        if (
            message.numberA < 0 ||
            message.numberA >
                BoardwalkSquare)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        const std::size_t squareNo =
            static_cast<std::size_t>(
                message.numberA
            );


        const GamePhase phase =
            phases::current(state).phase;


        const bool quick =
            message.numberD != 0;


        if (quick)
        {
            if (!quickMortgagePhaseAllowed(
                    phase))
            {
                notifyActionCompleted(
                    message,
                    false
                );

                return;
            }
        }
        else
        {
            if (
                phase !=
                    GamePhase::BuySellMortgage &&
                phase !=
                    GamePhase::FreeUnmortgage)
            {
                notifyActionCompleted(
                    message,
                    false
                );

                return;
            }


            if (
                phase ==
                    GamePhase::BuySellMortgage &&
                player !=
                    phases::current(
                        state
                    ).fromPlayer)
            {
                notifyActionCompleted(
                    message,
                    false
                );

                return;
            }


            if (
                phase ==
                    GamePhase::FreeUnmortgage &&
                player !=
                    phases::current(
                        state
                    ).toPlayer)
            {
                notifyActionCompleted(
                    message,
                    false
                );

                return;
            }
        }


        SquareState& square =
            state.squares[
                squareNo
            ];


        const board::SquareType squareType =
            static_cast<board::SquareType>(
                squareNo
            );


        if (square.owner != player)
        {
            notifyActionCompleted(
                message,
                false
            );


            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorMortgagingOnUnowned,
                0,
                player,
                squareNo
            );

            return;
        }


        const auto group =
            board::definition(
                squareType
            ).group;


        // Aucun bâtiment sur le groupe.

        for (std::size_t testNo = 0;
             testNo < SquareCount;
             ++testNo)
        {
            if (
                board::definition(
                    static_cast<board::SquareType>(
                        testNo
                    )
                ).group != group)
            {
                continue;
            }


            if (
                state.squares[
                    testNo
                ].houses > 0)
            {
                notifyActionCompleted(
                    message,
                    false
                );


                messaging::sendAction(
                    actions::Type::NotifyErrorMessage,
                    BankPlayer,
                    AllPlayers,
                    legacy_text::ErrorMortgagingHouses,
                    0,
                    player,
                    squareNo
                );

                return;
            }
        }


        const bool wasMortgaged =
            square.mortgaged;


        std::int64_t fees =
            board::definition(
                squareType
            ).mortgageCost;


        if (wasMortgaged)
        {
            // Dés-hypothèque.

            const bool noInterestFee =
                phase ==
                    GamePhase::FreeUnmortgage &&
                (
                    board::propertyBit(
                        squareType
                    ) &
                    static_cast<std::uint32_t>(
                        phases::current(
                            state
                        ).amount
                    )
                ) != 0;


            if (!noInterestFee)
            {
                fees +=
                    (
                        fees *
                        state.options.interestRate +
                        50
                    ) / 100;
            }


            if (
                state.players[
                    player
                ].cash < fees)
            {
                notifyActionCompleted(
                    message,
                    false
                );


                messaging::sendAction(
                    actions::Type::NotifyErrorMessage,
                    BankPlayer,
                    AllPlayers,
                    legacy_text::ErrorMortgageNoCash,
                    fees,
                    player,
                    squareNo
                );

                return;
            }


            blindlyTransferCash(
                state,
                player,
                BankPlayer,
                fees,
                true
            );


            if (noInterestFee)
            {
                state.phaseStack[0].amount &=
                    ~static_cast<std::int64_t>(
                        board::propertyBit(
                            squareType
                        )
                    );

                sendRestart();
            }
        }
        else
        {
            // Hypothèque :
            // la banque verse la valeur d'hypothèque.

            blindlyTransferCash(
                state,
                BankPlayer,
                player,
                fees,
                true
            );
        }


        const bool newMortgaged =
            !wasMortgaged;


        notifyActionCompleted(
            message,
            true,
            newMortgaged ? 1 : 0
        );


        messaging::sendAction(
            actions::Type::NotifySquareMortgage,
            BankPlayer,
            AllPlayers,
            squareNo,
            newMortgaged ? 1 : 0,
            wasMortgaged ? 1 : 0
        );


        messaging::sendAction(
            actions::Type::NotifyErrorMessage,
            BankPlayer,
            AllPlayers,
            wasMortgaged
                ? legacy_text::ErrorUnmortgageInfo
                : legacy_text::ErrorMortgageInfo,
            fees,
            player,
            squareNo
        );


        square.mortgaged =
            newMortgaged;


        // Quicky mortgage pendant une dette :
        // refaire immédiatement le test de solvabilité.

        if (
            quick &&
            !wasMortgaged &&
            phase ==
                GamePhase::CollectingPayment)
        {
            debtStateChanged = true;

            sendRestart();
        }
    }


    void actionSellBuildings(
        GameState& state,
        const actions::Message& message)
    {
        // Le chemin "sell everything" de
        // ActionSellBuildings() est porté intégralement ici.
        //
        // C'est aussi le chemin nécessaire pour liquider
        // rapidement un groupe pendant GF_COLLECTING_PAYMENT.

        const PlayerNumber player =
            message.fromPlayer;


        if (
            message.numberA < 0 ||
            message.numberA >
                BoardwalkSquare)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        const std::size_t squareNo =
            static_cast<std::size_t>(
                message.numberA
            );


        const GamePhase phase =
            phases::current(state).phase;


        const bool quick =
            message.numberD != 0;


        if (
            quick &&
            !quickSellPhaseAllowed(phase))
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        if (
            !quick &&
            phase !=
                GamePhase::BuySellMortgage &&
            phase !=
                GamePhase::DecomposeHotel)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        if (
            message.numberB == 0)
        {
            // La vente unitaire dépend de
            // RULE_TestBuildingPlacement() +
            // GF_DECOMPOSE_HOTEL.
            //
            // Nous ne contournons pas cette validation.
            // Le chemin complet viendra avec le moteur
            // de construction/housing shortage.

            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        if (
            state.squares[
                squareNo
            ].owner != player)
        {
            notifyActionCompleted(
                message,
                false
            );


            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorBuildingOnUnowned,
                0,
                player,
                squareNo
            );

            return;
        }


        notifyActionCompleted(
            message,
            true
        );


        sellAllBuildingsOnMonopoly(
            state,
            static_cast<board::SquareType>(
                squareNo
            )
        );


        if (
            quick &&
            phase ==
                GamePhase::CollectingPayment)
        {
            debtStateChanged = true;

            sendRestart();
        }
    }


    void actionGoBankrupt(
        GameState& state,
        const actions::Message& message)
    {
        // ActionGoBankrupt() :
        // cœur du comportement original.
        //
        // CountHits (futures/immunités) n'existe pas encore
        // dans notre GameState ; aucun de ces contrats ne peut
        // donc encore exister et rien n'est à annuler ici.

        const PlayerNumber player =
            message.fromPlayer;


        if (
            phases::current(state).phase !=
            GamePhase::CollectingPayment)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        if (
            player !=
            phases::current(
                state
            ).fromPlayer)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        if (
            cashPlayerCouldRaiseIfNeeded(
                state,
                player
            ) >=
            phases::current(
                state
            ).amount)
        {
            notifyActionCompleted(
                message,
                false
            );


            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorCannotGoBankrupt,
                phases::current(
                    state
                ).toPlayer,
                player,
                phases::current(
                    state
                ).amount
            );


            sendRestart();
            return;
        }


        notifyActionCompleted(
            message,
            true
        );


        // Si le joueur a hypothéqué/vendu depuis la demande
        // de paiement, revenir exactement à l'état au début
        // de cette dette.
        //
        // C'est l'équivalent moderne de
        // StackedRulesStates[0].

        const std::uint64_t duration =
            state.gameDurationInSeconds;


        if (
            debtStateChanged &&
            debtSnapshot.has_value())
        {
            state =
                *debtSnapshot;

            state.gameDurationInSeconds =
                duration;
        }


        const PendingPhase debt =
            phases::current(state);


        const PlayerNumber creditor =
            debt.toPlayer;


        std::int64_t mortgageAmount = 0;

        std::uint32_t oldProperties = 0;


        for (std::size_t squareNo = 0;
             squareNo < SquareCount;
             ++squareNo)
        {
            SquareState& square =
                state.squares[squareNo];


            if (
                square.owner != player &&
                !(
                    square.owner ==
                        EscrowPlayer &&
                    square.offeredInTradeTo ==
                        player
                ))
            {
                continue;
            }


            const board::SquareType squareType =
                static_cast<board::SquareType>(
                    squareNo
                );


            oldProperties |=
                board::propertyBit(
                    squareType
                );


            sellAllBuildingsOnMonopoly(
                state,
                squareType
            );


            if (square.mortgaged)
            {
                mortgageAmount +=
                    board::definition(
                        squareType
                    ).mortgageCost;
            }


            transferProperty(
                state,
                squareType,
                creditor
            );
        }


        // Tout le cash restant va au créancier.

        blindlyTransferCash(
            state,
            player,
            creditor,
            state.players[
                player
            ].cash,
            true
        );


        // Cartes sortie de prison.

        transferGetOutOfJail(
            state,
            player,
            creditor,
            DeckType::Chance
        );


        transferGetOutOfJail(
            state,
            player,
            creditor,
            DeckType::Community
        );


        // Cancel active rent contracts owned by bankrupt player.
        //
        // ActionGoBankrupt() original :
        // seul toPlayer est testé ; le test fromPlayer avait été
        // volontairement commenté dans le source final.

        for (CountHitRecord& hit :
             state.countHits)
        {
            if (
                !hit.tradedItem &&
                hit.toPlayer != NobodyPlayer &&
                hit.toPlayer == player)
            {
                actions::Message contractUpdate{};

                contractUpdate.action =
                    hit.hitType ==
                        CountHitType::RentImmunity
                        ? actions::Type::
                            NotifyImmunityCount
                        : actions::Type::
                            NotifyFutureRentCount;

                contractUpdate.fromPlayer =
                    BankPlayer;

                contractUpdate.toPlayer =
                    AllPlayers;

                contractUpdate.numberA =
                    hit.toPlayer;

                contractUpdate.numberB = 0;

                contractUpdate.numberC = 41;

                contractUpdate.numberD =
                    hit.fromPlayer;

                contractUpdate.numberE =
                    hit.properties;


                messaging::sendAction(
                    contractUpdate
                );


                hit.toPlayer =
                    NobodyPlayer;
            }
        }


        messaging::sendAction(
            actions::Type::NotifyErrorMessage,
            BankPlayer,
            AllPlayers,
            legacy_text::ErrorBankruptTo,
            creditor,
            player,
            debt.amount
        );


        // Retirer le joueur du plateau.

        messaging::sendAction(
            actions::Type::NotifyJumpToSquare,
            BankPlayer,
            AllPlayers,
            OffBoardSquare,
            state.players[
                player
            ].currentSquare,
            player
        );


        state.players[
            player
        ].currentSquare =
            OffBoardSquare;


        debtSnapshot.reset();
        debtStateChanged = false;


        // Si le créancier est un joueur, les propriétés
        // héritées passent par ESCROW et le créancier doit
        // payer les intérêts de transfert des hypothèques.

        if (creditor < MaxPlayers)
        {
            phases::switchTo(
                state,
                GamePhase::TransferEscrowProperty,
                0,
                creditor,
                oldProperties
            );


            stackDebtAndRestart(
                state,
                creditor,
                BankPlayer,
                (
                    mortgageAmount *
                    state.options.interestRate +
                    50
                ) / 100
            );


            // ActionGoBankrupt() vérifie immédiatement
            // s'il ne reste plus qu'un survivant, même si
            // les frais hypothécaires viennent d'être empilés.
            if (
                lifecycle::onlyOneSurvivor(
                    state
                ))
            {
                lifecycle::declareWinner(
                    state
                );
            }

            return;
        }


        // Faillite envers la banque :
        // aucune commission de transfert.

        popAndRestart(state);


        // ActionGoBankrupt() original :
        // victoire immédiate lorsqu'il ne reste qu'un joueur.

        if (
            lifecycle::onlyOneSurvivor(
                state
            ))
        {
            lifecycle::declareWinner(
                state
            );
        }
    }


    void landOnIncomeTax(
        GameState& state)
    {
        // ActionLandedOnSquare(SQ_INCOME_TAX).

        if (state.options.taxRate == 0)
        {
            phases::pop(state);


            stackDebtAndRestart(
                state,
                state.currentPlayer,
                BankPlayer,
                state.options.flatTaxFee
            );


            addMoneyToFreeParkingPot(
                state,
                state.options.flatTaxFee
            );


            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorTaxesCharged,
                state.options.flatTaxFee,
                state.currentPlayer
            );

            return;
        }


        phases::switchTo(
            state,
            GamePhase::FlatOrFractionTaxDecision,
            0,
            0,
            0
        );


        sendRestart();
    }


    void landOnLuxuryTax(
        GameState& state)
    {
        phases::pop(state);


        stackDebtAndRestart(
            state,
            state.currentPlayer,
            BankPlayer,
            state.options.luxuryTaxAmount
        );


        addMoneyToFreeParkingPot(
            state,
            state.options.luxuryTaxAmount
        );
    }


    void landOnFreeParking(
        GameState& state)
    {
        if (state.options.freeParkingPot)
        {
            const std::int64_t jackpot =
                state.freeParkingJackpotAmount;


            blindlyTransferCash(
                state,
                BankPlayer,
                state.currentPlayer,
                jackpot,
                true
            );


            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::FreeParkingPotCollected,
                jackpot,
                state.currentPlayer
            );


            state.freeParkingJackpotAmount = 0;


            // Commentaire original :
            // "Someone said that the parking pot gets
            // reseeded every time."

            addMoneyToFreeParkingPot(
                state,
                state.options.freeParkingSeed
            );
        }


        popAndRestart(state);
    }


    bool restartEconomyPhase(
        GameState& state,
        const actions::Message& message)
    {
        switch (
            phases::current(
                state
            ).phase)
        {
            case GamePhase::CollectingPayment:
                restartCollectingPayment(
                    state
                );
                return true;


            case GamePhase::TransferEscrowProperty:
                if (
                    message.fromPlayer !=
                    BankPlayer)
                {
                    return true;
                }

                restartTransferEscrowProperty(
                    state
                );
                return true;


            case GamePhase::FreeUnmortgage:
                restartFreeUnmortgage(
                    state
                );
                return true;


            default:
                return false;
        }
    }
}







