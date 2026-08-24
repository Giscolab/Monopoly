#include "RuleTrade.hpp"

#include "BoardRules.hpp"
#include "CardDeckRuntime.hpp"
#include "LegacyTextIds.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"
#include "RuleEconomy.hpp"
#include "RuleSynchronization.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace monopoly::rules::trade
{
    namespace
    {
        constexpr std::uint8_t OffBoardSquare = 41;

        // Rule.cpp :
        // -1 counter
        //  0 reject
        //  1 accept
        //  2 nothing
        //  3 non-involved AI accepted
        int tradeStatus = 2;


        void sendFive(
            actions::Type type,
            std::int64_t a,
            std::int64_t b,
            std::int64_t c,
            std::int64_t d,
            std::int64_t e)
        {
            actions::Message message{};

            message.action = type;
            message.fromPlayer = BankPlayer;
            message.toPlayer = AllPlayers;

            message.numberA = a;
            message.numberB = b;
            message.numberC = c;
            message.numberD = d;
            message.numberE = e;

            messaging::sendAction(message);
        }


        void completed(
            const actions::Message& message,
            bool success)
        {
            messaging::sendAction(
                actions::Type::NotifyActionCompleted,
                BankPlayer,
                AllPlayers,
                static_cast<std::int64_t>(
                    message.action
                ),
                success ? 1 : 0,
                message.fromPlayer
            );
        }


        bool activePlayer(
            const GameState& state,
            PlayerNumber player)
        {
            return
                player < state.numberOfPlayers &&
                state.players[player].currentSquare <
                    OffBoardSquare;
        }


        void wrongPhase(
            const GameState& state,
            const actions::Message& message)
        {
            completed(message, false);

            if (message.fromPlayer == BankPlayer)
            {
                return;
            }

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
                static_cast<std::int64_t>(
                    phases::current(state).phase
                )
            );
        }


        void wrongPlayer(
            const GameState& state,
            const actions::Message& message)
        {
            completed(message, false);

            if (message.fromPlayer == BankPlayer)
            {
                return;
            }

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
                static_cast<std::int64_t>(
                    phases::current(state).phase
                )
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


        void setEmptyTrade(
            GameState& state)
        {
            // SetEmptyTrade() original.
            //
            // Ne pas toucher aux anciennes propriétés déjà
            // détenues par ESCROW.

            for (SquareState& square :
                 state.squares)
            {
                if (square.owner < MaxPlayers)
                {
                    square.offeredInTradeTo =
                        NobodyPlayer;
                }
            }


            for (PlayerState& player :
                 state.players)
            {
                player.cashGivenInTrade.fill(0);
            }


            for (CardDeck& deck :
                 state.cards)
            {
                deck.jailOfferedInTradeTo =
                    NobodyPlayer;
            }


            for (CountHitRecord& hit :
                 state.countHits)
            {
                if (hit.tradedItem)
                {
                    hit.toPlayer =
                        NobodyPlayer;

                    hit.tradedItem =
                        false;
                }
            }
        }


        bool playerInvolved(
            const GameState& state,
            PlayerNumber player)
        {
            for (const SquareState& square :
                 state.squares)
            {
                if (
                    square.owner < MaxPlayers &&
                    square.offeredInTradeTo !=
                        NobodyPlayer)
                {
                    if (
                        square.owner == player ||
                        square.offeredInTradeTo ==
                            player)
                    {
                        return true;
                    }
                }
            }


            for (PlayerNumber from = 0;
                 from < MaxPlayers;
                 ++from)
            {
                for (PlayerNumber to = 0;
                     to < MaxPlayers;
                     ++to)
                {
                    if (
                        state.players[from]
                            .cashGivenInTrade[to] > 0 &&
                        (
                            from == player ||
                            to == player
                        ))
                    {
                        return true;
                    }
                }
            }


            for (const CardDeck& deck :
                 state.cards)
            {
                if (
                    deck.jailOfferedInTradeTo !=
                        NobodyPlayer &&
                    (
                        deck.jailOwner == player ||
                        deck.jailOfferedInTradeTo ==
                            player
                    ))
                {
                    return true;
                }
            }


            for (const CountHitRecord& hit :
                 state.countHits)
            {
                if (
                    hit.toPlayer != NobodyPlayer &&
                    hit.tradedItem &&
                    (
                        hit.fromPlayer == player ||
                        hit.toPlayer == player
                    ))
                {
                    return true;
                }
            }


            return false;
        }


        bool tradeContainsAnything(
            const GameState& state)
        {
            for (const SquareState& square :
                 state.squares)
            {
                if (
                    square.owner < MaxPlayers &&
                    square.offeredInTradeTo <
                        MaxPlayers)
                {
                    return true;
                }
            }


            for (const CardDeck& deck :
                 state.cards)
            {
                if (
                    deck.jailOfferedInTradeTo <
                    MaxPlayers)
                {
                    return true;
                }
            }


            for (PlayerNumber from = 0;
                 from < state.numberOfPlayers;
                 ++from)
            {
                for (PlayerNumber to = 0;
                     to < state.numberOfPlayers;
                     ++to)
                {
                    if (
                        state.players[from]
                            .cashGivenInTrade[to] > 0)
                    {
                        return true;
                    }
                }
            }


            for (const CountHitRecord& hit :
                 state.countHits)
            {
                if (
                    hit.toPlayer != NobodyPlayer &&
                    hit.tradedItem)
                {
                    return true;
                }
            }


            return false;
        }


        void resendTradeProposal(
            const GameState& state)
        {
            // ResendTradeProposal().

            messaging::sendAction(
                actions::Type::NotifyTradeStarted,
                BankPlayer,
                AllPlayers,
                phases::current(state).fromPlayer,
                tradeStatus
            );


            // Properties.

            for (std::size_t squareNo = 0;
                 squareNo < SquareCount;
                 ++squareNo)
            {
                const SquareState& square =
                    state.squares[squareNo];


                if (
                    square.owner < MaxPlayers &&
                    square.offeredInTradeTo !=
                        NobodyPlayer)
                {
                    sendFive(
                        actions::Type::NotifyTradeItem,
                        square.owner,
                        square.offeredInTradeTo,
                        static_cast<std::int64_t>(
                            TradeItemKind::Square
                        ),
                        squareNo,
                        0
                    );
                }
            }


            // Cash.

            for (PlayerNumber from = 0;
                 from < MaxPlayers;
                 ++from)
            {
                for (PlayerNumber to = 0;
                     to < MaxPlayers;
                     ++to)
                {
                    const std::int64_t amount =
                        state.players[from]
                            .cashGivenInTrade[to];


                    if (amount <= 0)
                    {
                        continue;
                    }


                    sendFive(
                        actions::Type::NotifyTradeItem,
                        from,
                        to,
                        static_cast<std::int64_t>(
                            TradeItemKind::Cash
                        ),
                        amount,
                        0
                    );
                }
            }


            // Jail cards.

            for (std::size_t deckNo = 0;
                 deckNo <
                    static_cast<std::size_t>(
                        DeckType::Count
                    );
                 ++deckNo)
            {
                const CardDeck& deck =
                    state.cards[deckNo];


                if (
                    deck.jailOfferedInTradeTo ==
                    NobodyPlayer)
                {
                    continue;
                }


                sendFive(
                    actions::Type::NotifyTradeItem,
                    deck.jailOwner,
                    deck.jailOfferedInTradeTo,
                    static_cast<std::int64_t>(
                        TradeItemKind::JailCard
                    ),
                    deckNo,
                    0
                );
            }


            // Immunities / futures.

            for (const CountHitRecord& hit :
                 state.countHits)
            {
                if (
                    hit.toPlayer == NobodyPlayer ||
                    !hit.tradedItem)
                {
                    continue;
                }


                sendFive(
                    actions::Type::NotifyTradeItem,
                    hit.fromPlayer,
                    hit.toPlayer,
                    static_cast<std::int64_t>(
                        hit.hitType ==
                            CountHitType::RentImmunity
                            ? TradeItemKind::Immunity
                            : TradeItemKind::FutureRent
                    ),
                    hit.hitCount,
                    hit.properties
                );
            }
        }


        void switchToTradeEditing(
            GameState& state,
            PlayerNumber newEditor)
        {
            const PlayerNumber proposer =
                phases::current(state)
                    .fromPlayer;


            phases::switchTo(
                state,
                GamePhase::EditingTrade,
                proposer,
                newEditor,
                0
            );


            sendRestart();
        }


        void cancelTrade(
            GameState& state,
            PlayerNumber player)
        {
            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorTradeRejected,
                0,
                player
            );


            setEmptyTrade(state);


            phases::switchTo(
                state,
                GamePhase::TradeFinished,
                0,
                0,
                0
            );


            sendRestart();
        }


        std::int64_t playerTradeAssets(
            const GameState& state,
            PlayerNumber player)
        {
            std::int64_t assets =
                state.players[player].cash;


            for (std::size_t squareNo = 0;
                 squareNo < SquareCount;
                 ++squareNo)
            {
                const SquareState& square =
                    state.squares[squareNo];


                if (
                    square.owner == player &&
                    square.offeredInTradeTo >=
                        MaxPlayers &&
                    !square.mortgaged)
                {
                    assets +=
                        board::definition(
                            static_cast<board::SquareType>(
                                squareNo
                            )
                        ).mortgageCost;
                }


                if (square.owner == player)
                {
                    assets +=
                        (
                            static_cast<std::int64_t>(
                                square.houses
                            ) *
                            board::definition(
                                static_cast<board::SquareType>(
                                    squareNo
                                )
                            ).housePurchaseCost
                            + 1
                        ) / 2;
                }
            }


            return assets;
        }


        std::int64_t playerTradeExpenses(
            const GameState& state,
            PlayerNumber player)
        {
            std::int64_t expenses = 0;


            // Intérêts sur propriétés hypothéquées reçues.

            for (std::size_t squareNo = 0;
                 squareNo < SquareCount;
                 ++squareNo)
            {
                const SquareState& square =
                    state.squares[squareNo];


                if (
                    square.owner < MaxPlayers &&
                    square.offeredInTradeTo ==
                        player &&
                    square.mortgaged)
                {
                    expenses +=
                        (
                            board::definition(
                                static_cast<board::SquareType>(
                                    squareNo
                                )
                            ).mortgageCost *
                            state.options.interestRate
                            + 50
                        ) / 100;
                }
            }


            // Cash donné.
            //
            // Le cash reçu n'est délibérément PAS compté comme
            // actif disponible : il peut arriver trop tard à cause
            // de l'ordre LIFO des dettes.

            for (PlayerNumber other = 0;
                 other < state.numberOfPlayers;
                 ++other)
            {
                expenses +=
                    state.players[player]
                        .cashGivenInTrade[other];
            }


            return expenses;
        }


        void settleAcceptedTrade(
            GameState& state)
        {
            const PlayerNumber proposer =
                phases::current(state)
                    .fromPlayer;


            phases::switchTo(
                state,
                GamePhase::TradeFinished,
                0,
                0,
                0
            );


            std::array<std::uint32_t, MaxPlayers>
                propertiesTransferred{};


            std::array<std::int64_t, MaxPlayers>
                mortgageTransferred{};


            // ------------------------------------------------
            // Properties -> ESCROW.
            // Buildings are sold automatically first.
            // ------------------------------------------------

            for (std::size_t squareNo = 0;
                 squareNo < SquareCount;
                 ++squareNo)
            {
                SquareState& square =
                    state.squares[squareNo];


                const PlayerNumber to =
                    square.offeredInTradeTo;


                if (
                    square.owner >= MaxPlayers ||
                    to >= MaxPlayers)
                {
                    continue;
                }


                const auto squareType =
                    static_cast<board::SquareType>(
                        squareNo
                    );


                propertiesTransferred[to] |=
                    board::propertyBit(
                        squareType
                    );


                economy::sellAllBuildingsForSettlement(
                    state,
                    static_cast<std::uint8_t>(
                        squareNo
                    )
                );


                if (square.mortgaged)
                {
                    mortgageTransferred[to] +=
                        board::definition(
                            squareType
                        ).mortgageCost;
                }


                economy::transferPropertyForSettlement(
                    state,
                    static_cast<std::uint8_t>(
                        squareNo
                    ),
                    to
                );
            }


            // ------------------------------------------------
            // Escrow + intérêts.
            //
            // Ordre cyclique descendant depuis le proposant,
            // exactement comme Rule.cpp.
            // ------------------------------------------------

            if (state.numberOfPlayers > 0)
            {
                for (std::size_t offset = 0;
                     offset <
                        state.numberOfPlayers;
                     ++offset)
                {
                    const PlayerNumber player =
                        static_cast<PlayerNumber>(
                            (
                                proposer +
                                state.numberOfPlayers -
                                offset
                            ) %
                            state.numberOfPlayers
                        );


                    if (
                        propertiesTransferred[
                            player
                        ] != 0)
                    {
                        phases::push(
                            state,
                            GamePhase::
                                TransferEscrowProperty,
                            0,
                            player,
                            propertiesTransferred[
                                player
                            ]
                        );
                    }


                    if (
                        mortgageTransferred[
                            player
                        ] > 0)
                    {
                        economy::stackDebt(
                            state,
                            player,
                            BankPlayer,
                            (
                                mortgageTransferred[
                                    player
                                ] *
                                state.options.interestRate
                                + 50
                            ) / 100
                        );
                    }
                }
            }


            // ------------------------------------------------
            // Jail cards.
            // ------------------------------------------------

            for (std::size_t deckNo = 0;
                 deckNo <
                    static_cast<std::size_t>(
                        DeckType::Count
                    );
                 ++deckNo)
            {
                const PlayerNumber to =
                    state.cards[deckNo]
                        .jailOfferedInTradeTo;


                if (to < MaxPlayers)
                {
                    cardruntime::transferGetOutOfJail(
                        state,
                        state.cards[deckNo]
                            .jailOwner,
                        to,
                        static_cast<DeckType>(
                            deckNo
                        )
                    );
                }
            }


            // ------------------------------------------------
            // Cash.
            // ------------------------------------------------

            if (state.numberOfPlayers > 0)
            {
                for (std::size_t fromOffset = 0;
                     fromOffset <
                        state.numberOfPlayers;
                     ++fromOffset)
                {
                    const PlayerNumber from =
                        static_cast<PlayerNumber>(
                            (
                                proposer +
                                state.numberOfPlayers -
                                fromOffset
                            ) %
                            state.numberOfPlayers
                        );


                    for (std::size_t toOffset = 0;
                         toOffset <
                            state.numberOfPlayers;
                         ++toOffset)
                    {
                        const PlayerNumber to =
                            static_cast<PlayerNumber>(
                                (
                                    from +
                                    state.numberOfPlayers -
                                    toOffset
                                ) %
                                state.numberOfPlayers
                            );


                        const std::int64_t cash =
                            state.players[from]
                                .cashGivenInTrade[to];


                        if (cash > 0)
                        {
                            economy::stackDebt(
                                state,
                                from,
                                to,
                                cash
                            );
                        }
                    }
                }
            }


            sendRestart();
        }


        void finishTrade(
            GameState& state)
        {
            state.tradeInProgress =
                false;


            // ------------------------------------------------
            // Activer les nouveaux futures / immunités.
            //
            // Duplicates :
            // mêmes properties + toPlayer + hitType.
            //
            // Le test fromPlayer est COMMENTÉ dans le source.
            // ------------------------------------------------

            for (std::size_t i = 0;
                 i < state.countHits.size();
                 ++i)
            {
                CountHitRecord& current =
                    state.countHits[i];


                if (
                    current.toPlayer ==
                        NobodyPlayer ||
                    !current.tradedItem)
                {
                    continue;
                }


                int count =
                    current.hitCount;


                for (std::size_t j = 0;
                     j < state.countHits.size();
                     ++j)
                {
                    if (j == i)
                    {
                        continue;
                    }


                    CountHitRecord& other =
                        state.countHits[j];


                    if (
                        other.properties ==
                            current.properties &&
                        other.toPlayer ==
                            current.toPlayer &&
                        other.hitType ==
                            current.hitType)
                    {
                        count +=
                            other.hitCount;


                        other.toPlayer =
                            NobodyPlayer;
                    }
                }


                count =
                    std::clamp(
                        count,
                        0,
                        127
                    );


                current.hitCount =
                    count;

                current.tradedItem =
                    false;


                sendFive(
                    current.hitType ==
                        CountHitType::RentImmunity
                        ? actions::Type::
                            NotifyImmunityCount
                        : actions::Type::
                            NotifyFutureRentCount,
                    current.toPlayer,
                    count,
                    OffBoardSquare,
                    current.fromPlayer,
                    current.properties
                );


                sendFive(
                    actions::Type::NotifyErrorMessage,
                    current.hitType ==
                        CountHitType::RentImmunity
                        ? legacy_text::
                            ErrorImmunityGranted
                        : legacy_text::
                            ErrorFutureRentGranted,
                    current.fromPlayer,
                    current.toPlayer,
                    count,
                    current.properties
                );


                if (count <= 0)
                {
                    current.toPlayer =
                        NobodyPlayer;
                }
            }


            messaging::sendAction(
                actions::Type::NotifyTradeFinished,
                BankPlayer,
                AllPlayers,
                tradeStatus
            );


            tradeStatus = 2;


            phases::pop(state);

            sendRestart();
        }
    }


    void resetTransientState()
    {
        tradeStatus = 2;
    }


    void actionStartTradeEditing(
        GameState& state,
        const actions::Message& message)
    {
        const PlayerNumber player =
            message.fromPlayer;


        if (!activePlayer(state, player))
        {
            wrongPlayer(state, message);
            return;
        }


        const GamePhase phase =
            phases::current(state).phase;


        const bool allowed =
            phase == GamePhase::WaitMoveRoll ||
            phase == GamePhase::WaitEndTurn ||
            phase == GamePhase::WaitJailRoll ||
            phase == GamePhase::WaitUtilityRoll ||
            phase == GamePhase::WaitUntilCardSeen ||
            phase ==
                GamePhase::
                    JailRollOrPayOrCardDecision ||
            phase ==
                GamePhase::
                    FlatOrFractionTaxDecision ||
            phase ==
                GamePhase::
                    AuctionOrBuyDecision ||
            phase ==
                GamePhase::
                    CollectingPayment ||
            phase ==
                GamePhase::
                    FreeUnmortgage ||
            phase ==
                GamePhase::
                    TradeAcceptance;


        if (!allowed)
        {
            wrongPhase(state, message);
            return;
        }


        // ----------------------------------------------------
        // Reprendre l'édition pendant ACCEPTANCE.
        // ----------------------------------------------------

        if (
            phase ==
            GamePhase::TradeAcceptance)
        {
            if (
                phases::current(state).amount != 0 &&
                !playerInvolved(
                    state,
                    player
                ))
            {
                messaging::sendAction(
                    actions::Type::NotifyErrorMessage,
                    BankPlayer,
                    AllPlayers,
                    legacy_text::
                        ErrorTradeCantEditDuringAccept,
                    0,
                    player
                );


                completed(
                    message,
                    false
                );

                return;
            }


            completed(
                message,
                true
            );


            switchToTradeEditing(
                state,
                player
            );


            return;
        }


        // ----------------------------------------------------
        // Une transaction précédente doit être entièrement
        // réglée avant d'en démarrer une autre.
        // ----------------------------------------------------

        if (state.tradeInProgress)
        {
            completed(
                message,
                false
            );


            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                player,
                legacy_text::
                    ErrorCantTradeUntilSettled,
                0,
                player
            );


            return;
        }


        setEmptyTrade(state);


        phases::push(
            state,
            GamePhase::EditingTrade,
            player,
            player,
            0
        );


        state.tradeInProgress =
            true;


        // ActionStartTradeEditing() original :
        // IgnoreWaitForEverybodyReady = FALSE.
        sync::resetWaitGate();


        sendRestart();


        completed(
            message,
            true
        );
    }


    void actionTradeItem(
        GameState& state,
        const actions::Message& message)
    {
        const PlayerNumber editor =
            message.fromPlayer;


        if (
            phases::current(state).phase !=
                GamePhase::EditingTrade)
        {
            wrongPhase(state, message);
            return;
        }


        if (
            editor !=
            phases::current(state).toPlayer)
        {
            wrongPlayer(state, message);
            return;
        }


        PlayerNumber from =
            static_cast<PlayerNumber>(
                message.numberA
            );


        PlayerNumber to =
            static_cast<PlayerNumber>(
                message.numberB
            );


        const auto kind =
            static_cast<TradeItemKind>(
                message.numberC
            );


        std::int64_t value =
            message.numberD;


        const std::uint32_t propertySet =
            static_cast<std::uint32_t>(
                message.numberE
            );


        bool success = true;
        bool outOfContracts = false;


        if (!activePlayer(state, to))
        {
            success = false;
        }


        if (success)
        {
            switch (kind)
            {
                case TradeItemKind::Cash:
                {
                    if (
                        !activePlayer(
                            state,
                            from
                        ) ||
                        value < 0)
                    {
                        success = false;
                        break;
                    }


                    // Pas d'argent simultanément dans les
                    // deux directions.

                    if (
                        state.players[to]
                            .cashGivenInTrade[from] != 0)
                    {
                        state.players[to]
                            .cashGivenInTrade[from] = 0;


                        sendFive(
                            actions::Type::
                                NotifyTradeItem,
                            to,
                            from,
                            static_cast<std::int64_t>(
                                TradeItemKind::Cash
                            ),
                            0,
                            0
                        );
                    }


                    state.players[from]
                        .cashGivenInTrade[to] =
                        value;


                    break;
                }


                case TradeItemKind::Square:
                {
                    if (
                        value < 0 ||
                        value >=
                            static_cast<std::int64_t>(
                                SquareCount
                            ))
                    {
                        success = false;
                        break;
                    }


                    const auto squareType =
                        static_cast<
                            board::SquareType
                        >(value);


                    if (
                        board::propertyBit(
                            squareType
                        ) == 0)
                    {
                        success = false;
                        break;
                    }


                    SquareState& square =
                        state.squares[
                            static_cast<std::size_t>(
                                value
                            )
                        ];


                    from =
                        square.owner;


                    if (from >= MaxPlayers)
                    {
                        success = false;
                        break;
                    }


                    if (from == to)
                    {
                        square.offeredInTradeTo =
                            NobodyPlayer;

                        to = NobodyPlayer;
                    }
                    else
                    {
                        square.offeredInTradeTo =
                            to;
                    }


                    break;
                }


                case TradeItemKind::JailCard:
                {
                    if (
                        value < 0 ||
                        value >=
                            static_cast<std::int64_t>(
                                DeckType::Count
                            ))
                    {
                        success = false;
                        break;
                    }


                    CardDeck& deck =
                        state.cards[
                            static_cast<std::size_t>(
                                value
                            )
                        ];


                    from =
                        deck.jailOwner;


                    if (from >= MaxPlayers)
                    {
                        success = false;
                        break;
                    }


                    if (from == to)
                    {
                        deck.jailOfferedInTradeTo =
                            NobodyPlayer;

                        to = NobodyPlayer;
                    }
                    else
                    {
                        deck.jailOfferedInTradeTo =
                            to;
                    }


                    break;
                }


                case TradeItemKind::Immunity:
                case TradeItemKind::FutureRent:
                {
                    if (
                        !activePlayer(
                            state,
                            from
                        ) ||
                        propertySet == 0)
                    {
                        success = false;
                        break;
                    }


                    if (
                        kind ==
                            TradeItemKind::Immunity &&
                        !state.options
                            .immunitiesTradingAllowed)
                    {
                        success = false;
                        break;
                    }


                    if (
                        kind ==
                            TradeItemKind::FutureRent &&
                        !state.options
                            .futureRentTradingAllowed)
                    {
                        success = false;
                        break;
                    }


                    value =
                        std::clamp<std::int64_t>(
                            value,
                            -128,
                            127
                        );


                    const CountHitType hitType =
                        kind ==
                            TradeItemKind::FutureRent
                            ? CountHitType::FutureRent
                            : CountHitType::RentImmunity;


                    CountHitRecord* found =
                        nullptr;

                    CountHitRecord* free =
                        nullptr;


                    for (CountHitRecord& hit :
                         state.countHits)
                    {
                        if (
                            hit.properties ==
                                propertySet &&
                            hit.toPlayer == to &&
                            hit.hitType ==
                                hitType &&
                            hit.tradedItem)
                        {
                            found = &hit;
                            break;
                        }


                        if (
                            free == nullptr &&
                            hit.toPlayer ==
                                NobodyPlayer)
                        {
                            free = &hit;
                        }
                    }


                    if (found != nullptr)
                    {
                        if (value != 0)
                        {
                            found->hitCount =
                                static_cast<std::int32_t>(
                                    value
                                );
                        }
                        else
                        {
                            found->toPlayer =
                                NobodyPlayer;

                            found->tradedItem =
                                false;
                        }
                    }
                    else if (free != nullptr)
                    {
                        free->properties =
                            propertySet;

                        free->fromPlayer =
                            from;

                        free->toPlayer =
                            to;

                        free->hitType =
                            hitType;

                        free->tradedItem =
                            true;

                        free->hitCount =
                            static_cast<std::int32_t>(
                                value
                            );
                    }
                    else
                    {
                        success = false;
                        outOfContracts = true;
                    }


                    break;
                }


                default:
                    success = false;
                    break;
            }
        }


        if (!success)
        {
            completed(
                message,
                false
            );


            if (outOfContracts)
            {
                messaging::sendAction(
                    actions::Type::NotifyErrorMessage,
                    BankPlayer,
                    AllPlayers,
                    legacy_text::ErrorImmunityOutOf,
                    MaxCountHitSets,
                    editor
                );
            }
            else
            {
                messaging::sendAction(
                    actions::Type::NotifyErrorMessage,
                    BankPlayer,
                    editor,
                    legacy_text::ErrorCantTradeThat,
                    0,
                    editor
                );
            }


            return;
        }


        completed(
            message,
            true
        );


        // Le dernier joueur ayant modifié l'offre devient
        // le nouveau proposant.

        if (
            phases::current(state).fromPlayer !=
            editor)
        {
            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                editor,
                legacy_text::ErrorTradeChanging,
                0,
                editor
            );


            state.phaseStack[0]
                .fromPlayer =
                editor;
        }


        sendFive(
            actions::Type::NotifyTradeItem,
            from,
            to,
            static_cast<std::int64_t>(
                kind
            ),
            value,
            propertySet
        );


        // ----------------------------------------------------
        // Avertissement :
        // les bâtiments du monopole seront vendus.
        // ----------------------------------------------------

        if (
            kind ==
                TradeItemKind::Square &&
            value >= 0 &&
            value <
                static_cast<std::int64_t>(
                    SquareCount
                ) &&
            state.squares[
                static_cast<std::size_t>(
                    value
                )
            ].offeredInTradeTo !=
                NobodyPlayer)
        {
            const board::SquareGroup group =
                board::definition(
                    static_cast<board::SquareType>(
                        value
                    )
                ).group;


            for (std::size_t squareNo = 0;
                 squareNo < SquareCount;
                 ++squareNo)
            {
                if (
                    board::definition(
                        static_cast<board::SquareType>(
                            squareNo
                        )
                    ).group == group &&
                    state.squares[
                        squareNo
                    ].houses > 0)
                {
                    messaging::sendAction(
                        actions::Type::
                            NotifyErrorMessage,
                        BankPlayer,
                        editor,
                        legacy_text::
                            ErrorTradeHouseSale,
                        0,
                        state.squares[
                            squareNo
                        ].owner,
                        squareNo
                    );


                    break;
                }
            }
        }
    }


    void actionClearTradeItems(
        GameState& state,
        const actions::Message& message)
    {
        if (
            phases::current(state).phase !=
            GamePhase::EditingTrade)
        {
            wrongPhase(state, message);
            return;
        }


        if (
            message.fromPlayer !=
            phases::current(state).toPlayer)
        {
            wrongPlayer(state, message);
            return;
        }


        setEmptyTrade(state);


        completed(
            message,
            true
        );


        sendRestart();
    }


    void actionClearTradedContracts(
        GameState& state,
        const actions::Message& message)
    {
        if (
            phases::current(state).phase !=
            GamePhase::EditingTrade)
        {
            wrongPhase(state, message);
            return;
        }


        if (
            message.fromPlayer !=
            phases::current(state).toPlayer)
        {
            wrongPlayer(state, message);
            return;
        }


        completed(
            message,
            true
        );


        // numberB = TO player.
        // numberC = CountHitType.
        //
        // numberA/fromPlayer est volontairement ignoré dans
        // le Rule.cpp final.

        if (message.numberB >= MaxPlayers)
        {
            return;
        }


        for (CountHitRecord& hit :
             state.countHits)
        {
            if (
                hit.toPlayer ==
                    static_cast<PlayerNumber>(
                        message.numberB
                    ) &&
                hit.hitType ==
                    static_cast<CountHitType>(
                        message.numberC
                    ) &&
                hit.tradedItem)
            {
                sendFive(
                    actions::Type::NotifyTradeItem,
                    hit.fromPlayer,
                    hit.toPlayer,
                    static_cast<std::int64_t>(
                        hit.hitType ==
                            CountHitType::FutureRent
                            ? TradeItemKind::FutureRent
                            : TradeItemKind::Immunity
                    ),
                    0,
                    hit.properties
                );


                hit.toPlayer =
                    NobodyPlayer;
            }
        }
    }


    void actionTradeEditingDone(
        GameState& state,
        const actions::Message& message)
    {
        const PlayerNumber editor =
            message.fromPlayer;


        if (
            phases::current(state).phase !=
            GamePhase::EditingTrade)
        {
            wrongPhase(state, message);
            return;
        }


        if (
            editor !=
            phases::current(state).toPlayer)
        {
            wrongPlayer(state, message);
            return;
        }


        completed(
            message,
            true
        );


        std::int64_t opCode =
            message.numberA;


        if (
            opCode == 0 &&
            !tradeContainsAnything(state))
        {
            // Le source convertit automatiquement
            // "vote sur trade vide" en annulation.
            opCode = 2;
        }


        switch (opCode)
        {
            // ------------------------------------------------
            // Soumettre au vote.
            // ------------------------------------------------

            case 0:
            {
                for (PlayerState& player :
                     state.players)
                {
                    player.tradeAccepted =
                        false;
                }


                const PlayerNumber proposer =
                    phases::current(state)
                        .fromPlayer;


                phases::switchTo(
                    state,
                    GamePhase::TradeAcceptance,
                    proposer,
                    0,
                    message.numberB != 0
                        ? 1
                        : 0
                );


                sendRestart();


                // Le joueur qui soumet le deal
                // l'accepte automatiquement via
                // ActionTradeAccept(), exactement comme le faux
                // message du source.

                actions::Message automaticAccept{};

                automaticAccept.action =
                    actions::Type::Null;

                automaticAccept.fromPlayer =
                    editor;

                automaticAccept.toPlayer =
                    BankPlayer;

                automaticAccept.numberA = 1;
                automaticAccept.numberB = 1;


                actionTradeAccept(
                    state,
                    automaticAccept
                );


                break;
            }


            // ------------------------------------------------
            // Passer l'édition à un autre joueur.
            // ------------------------------------------------

            case 1:
            {
                const PlayerNumber nextEditor =
                    static_cast<PlayerNumber>(
                        message.numberB
                    );


                if (
                    activePlayer(
                        state,
                        nextEditor
                    ))
                {
                    state.phaseStack[0]
                        .toPlayer =
                        nextEditor;


                    messaging::sendAction(
                        actions::Type::
                            NotifyTradeEditor,
                        BankPlayer,
                        AllPlayers,
                        nextEditor
                    );
                }
                else
                {
                    wrongPlayer(
                        state,
                        message
                    );
                }


                break;
            }


            // ------------------------------------------------
            // Annuler.
            // ------------------------------------------------

            case 2:
            {
                cancelTrade(
                    state,
                    editor
                );

                break;
            }


            default:
                sendRestart();
                break;
        }
    }


    void actionTradeAccept(
        GameState& state,
        const actions::Message& message)
    {
        if (
            phases::current(state).phase !=
            GamePhase::TradeAcceptance)
        {
            wrongPhase(state, message);
            return;
        }


        const PlayerNumber player =
            message.fromPlayer;


        const bool privateTrade =
            phases::current(state).amount != 0;


        if (
            !activePlayer(
                state,
                player
            ) ||
            (
                privateTrade &&
                !playerInvolved(
                    state,
                    player
                )
            ))
        {
            // Cas normal pour un AI extérieur à un deal privé :
            // il est simplement ignoré.

            completed(
                message,
                false
            );

            return;
        }


        // Un non-participant ne peut pas rejeter une offre
        // publique ; il peut seulement accepter ou l'éditer.

        if (
            !playerInvolved(
                state,
                player
            ) &&
            message.numberA == 0)
        {
            wrongPlayer(
                state,
                message
            );

            return;
        }


        if (message.numberB != 3)
        {
            tradeStatus =
                static_cast<int>(
                    message.numberB
                );
        }


        completed(
            message,
            true
        );


        // ----------------------------------------------------
        // Rejet.
        // ----------------------------------------------------

        if (message.numberA == 0)
        {
            cancelTrade(
                state,
                player
            );

            return;
        }


        // ----------------------------------------------------
        // Validation juridique :
        //
        // chaque participant doit soit :
        //   - donner ET recevoir
        //   - ne rien donner ET ne rien recevoir
        // ----------------------------------------------------

        bool gettingSomething = false;
        bool givingSomething = false;


        for (const SquareState& square :
             state.squares)
        {
            if (
                square.owner < MaxPlayers &&
                square.offeredInTradeTo ==
                    player)
            {
                gettingSomething = true;
            }
            else if (
                square.offeredInTradeTo <
                    MaxPlayers &&
                square.owner ==
                    player)
            {
                givingSomething = true;
            }
        }


        for (const CardDeck& deck :
             state.cards)
        {
            if (
                deck.jailOfferedInTradeTo ==
                player)
            {
                gettingSomething = true;
            }
            else if (
                deck.jailOfferedInTradeTo <
                    MaxPlayers &&
                deck.jailOwner ==
                    player)
            {
                givingSomething = true;
            }
        }


        for (const CountHitRecord& hit :
             state.countHits)
        {
            if (
                hit.toPlayer != NobodyPlayer &&
                hit.tradedItem)
            {
                if (hit.toPlayer == player)
                {
                    gettingSomething = true;
                }

                if (hit.fromPlayer == player)
                {
                    givingSomething = true;
                }
            }
        }


        for (PlayerNumber from = 0;
             from < state.numberOfPlayers;
             ++from)
        {
            for (PlayerNumber to = 0;
                 to < state.numberOfPlayers;
                 ++to)
            {
                const std::int64_t cash =
                    state.players[from]
                        .cashGivenInTrade[to];


                if (cash <= 0)
                {
                    continue;
                }


                if (from == player)
                {
                    givingSomething = true;
                }

                if (to == player)
                {
                    gettingSomething = true;
                }
            }
        }


        if (
            givingSomething !=
            gettingSomething)
        {
            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorTradeGiveAndGet,
                0,
                player
            );


            setEmptyTrade(state);


            phases::switchTo(
                state,
                GamePhase::TradeFinished,
                0,
                0,
                0
            );


            sendRestart();

            return;
        }


        // ----------------------------------------------------
        // Le deal ne doit pas rendre le joueur insolvable.
        // ----------------------------------------------------

        const std::int64_t assets =
            playerTradeAssets(
                state,
                player
            );


        const std::int64_t expenses =
            playerTradeExpenses(
                state,
                player
            );


        if (expenses > assets)
        {
            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                player,
                legacy_text::ErrorTradeBankruptcy,
                expenses,
                player,
                assets
            );


            setEmptyTrade(state);


            phases::switchTo(
                state,
                GamePhase::TradeFinished,
                0,
                0,
                0
            );


            sendRestart();

            return;
        }


        state.players[player]
            .tradeAccepted =
            true;


        messaging::sendAction(
            actions::Type::NotifyErrorMessage,
            BankPlayer,
            AllPlayers,
            legacy_text::ErrorTradeAccepted,
            0,
            player
        );


        // ----------------------------------------------------
        // Tout le monde a accepté ?
        // ----------------------------------------------------

        bool allAccepted = true;


        for (PlayerNumber testPlayer = 0;
             testPlayer <
                state.numberOfPlayers;
             ++testPlayer)
        {
            if (
                !activePlayer(
                    state,
                    testPlayer
                ))
            {
                continue;
            }


            if (
                privateTrade &&
                !playerInvolved(
                    state,
                    testPlayer
                ))
            {
                continue;
            }


            if (
                !state.players[
                    testPlayer
                ].tradeAccepted)
            {
                allAccepted = false;
                break;
            }
        }


        if (!allAccepted)
        {
            sendRestart();
            return;
        }


        settleAcceptedTrade(
            state
        );
    }


    bool restartTradePhase(
        GameState& state,
        const actions::Message& message)
    {
        switch (
            phases::current(state).phase)
        {
            // ------------------------------------------------
            // GF_EDITING_TRADE
            // ------------------------------------------------

            case GamePhase::EditingTrade:
            {
                resendTradeProposal(
                    state
                );


                // Premier passage :
                // synchroniser tous les clients.
                if (
                    sync::beginWaitOnce(
                        state,
                        actions::Type::
                            NotifyTradeStarted
                    ))
                {
                    return true;
                }


                // Second passage :
                // le trade est resynchronisé, on annonce
                // l'éditeur réel.

                messaging::sendAction(
                    actions::Type::NotifyTradeEditor,
                    BankPlayer,
                    AllPlayers,
                    phases::current(state)
                        .toPlayer
                );


                return true;
            }


            // ------------------------------------------------
            // GF_TRADE_ACCEPTANCE
            // ------------------------------------------------

            case GamePhase::TradeAcceptance:
            {
                std::uint32_t playerSet = 0;
                std::uint32_t involvedSet = 0;


                const bool privateTrade =
                    phases::current(state).amount != 0;


                for (PlayerNumber player = 0;
                     player <
                        state.numberOfPlayers;
                     ++player)
                {
                    if (
                        !activePlayer(
                            state,
                            player
                        ))
                    {
                        continue;
                    }


                    const bool involved =
                        playerInvolved(
                            state,
                            player
                        );


                    if (involved)
                    {
                        involvedSet |=
                            1u << player;
                    }


                    if (
                        !state.players[player]
                            .tradeAccepted &&
                        (
                            !privateTrade ||
                            involved
                        ))
                    {
                        playerSet |=
                            1u << player;
                    }
                }


                messaging::sendAction(
                    actions::Type::
                        NotifyTradeAcceptanceDecision,
                    BankPlayer,
                    AllPlayers,
                    playerSet,
                    involvedSet
                );


                return true;
            }


            // ------------------------------------------------
            // GF_TRADE_FINISHED
            // ------------------------------------------------

            case GamePhase::TradeFinished:
            {
                // Le source ignore les restart externes.

                if (
                    message.fromPlayer !=
                    BankPlayer)
                {
                    return true;
                }


                // Attendre que toutes les notifications,
                // dettes et escrows aient quitté la queue.

                if (
                    messaging::currentQueueSize() != 0)
                {
                    return true;
                }


                finishTrade(state);

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
        // GF_TRADE_FINISHED attend la queue vide,
        // puis injecte ACTION_RESTART_PHASE.

        if (
            state.numberOfPendingPhases == 0 ||
            phases::current(state).phase !=
                GamePhase::TradeFinished)
        {
            return;
        }


        if (
            messaging::currentQueueSize() != 0)
        {
            return;
        }


        sendRestart();
    }
}


