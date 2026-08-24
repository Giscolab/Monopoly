#include "RuleAuction.hpp"

#include "BoardRules.hpp"
#include "LegacyTextIds.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"
#include "RuleBuildings.hpp"
#include "RuleEconomy.hpp"
#include "RuleSynchronization.hpp"

#include <cstddef>
#include <cstdint>

namespace monopoly::rules::auction
{
    namespace
    {
        constexpr std::uint8_t AuctionHouse = 40;
        constexpr std::uint8_t AuctionHotel = 41;
        constexpr std::uint8_t OffBoardSquare = 41;


        void sendRestart()
        {
            messaging::sendAction(
                actions::Type::RestartPhase,
                BankPlayer,
                BankPlayer
            );
        }


        void popAndRestart(
            GameState& state)
        {
            phases::pop(state);
            sendRestart();
        }


        void notifyActionCompleted(
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
                message.fromPlayer
            );
        }


        void sendFiveNumberAction(
            actions::Type action,
            std::int64_t a,
            std::int64_t b,
            std::int64_t c,
            std::int64_t d,
            std::int64_t e)
        {
            actions::Message message{};

            message.action = action;
            message.fromPlayer = BankPlayer;
            message.toPlayer = AllPlayers;

            message.numberA = a;
            message.numberB = b;
            message.numberC = c;
            message.numberD = d;
            message.numberE = e;

            messaging::sendAction(message);
        }


        buildings::PlayerSet playersAllowedToBid(
            const GameState& state)
        {
            if (
                state.numberOfPendingPhases == 0 ||
                state.phaseStack[0].phase !=
                    GamePhase::Auction)
            {
                return 0;
            }


            if (
                state.auction.propertyBeingAuctioned >=
                    AuctionHouse)
            {
                return
                    buildings::playersWhoCanBuyBuilding(
                        state,
                        state.auction
                            .propertyBeingAuctioned ==
                            AuctionHotel
                    );
            }


            buildings::PlayerSet allowed = 0;


            for (PlayerNumber player = 0;
                 player < state.numberOfPlayers;
                 ++player)
            {
                if (
                    state.players[
                        player
                    ].currentSquare <
                        OffBoardSquare)
                {
                    allowed |=
                        (1u << player);
                }
            }


            return allowed;
        }


        void initializeAuctionDataAndRestart(
            GameState& state)
        {
            // Les deux hacks présents dans Rule.cpp
            // sont désactivés dans ce build original.

            state.auction.tickCount = 0;
            state.auction.goingCount = 0;

            state.auction.propertyBeingAuctioned =
                static_cast<std::uint8_t>(
                    phases::current(
                        state
                    ).amount
                );

            state.auction.highestBidder =
                BankPlayer;

            state.auction.highestBid = 0;

            // InitialiseAuctionDataAndRestart():
            // IgnoreWaitForEverybodyReady = FALSE.
            sync::resetWaitGate();


            // WaitForEverybodyReady() est un mécanisme
            // réseau. MESS est encore local uniquement,
            // donc aucun faux handshake n'est injecté.

            sendRestart();
        }


        void finishAuction(
            GameState& state)
        {
            const PlayerNumber winner =
                state.auction.highestBidder;

            const std::uint8_t item =
                state.auction
                    .propertyBeingAuctioned;

            const std::int64_t price =
                state.auction.highestBid;


            if (winner < MaxPlayers)
            {
                // ------------------------------------------------
                // Propriété.
                // ------------------------------------------------

                if (item < AuctionHouse)
                {
                    economy::
                        transferPropertyForSettlement(
                            state,
                            item,
                            winner
                        );


                    phases::switchTo(
                        state,
                        GamePhase::
                            TransferEscrowProperty,
                        0,
                        winner,
                        board::propertyBit(
                            static_cast<
                                board::SquareType
                            >(item)
                        )
                    );


                    economy::stackDebtAndRestart(
                        state,
                        winner,
                        BankPlayer,
                        price
                    );


                    return;
                }


                // ------------------------------------------------
                // Maison / hôtel.
                //
                // Le bâtiment est déjà retiré du stock
                // logique pendant GF_PLACE_BUILDING :
                // on demande d'abord où le placer, puis
                // la dette d'enchère reste dessous.
                // ------------------------------------------------

                phases::pop(state);


                economy::stackDebt(
                    state,
                    winner,
                    BankPlayer,
                    price
                );


                phases::push(
                    state,
                    GamePhase::PlaceBuilding,
                    winner,
                    NobodyPlayer,
                    item == AuctionHotel
                        ? 1
                        : -1
                );


                sendRestart();

                return;
            }


            // ----------------------------------------------------
            // Personne n'a enchéri.
            // ----------------------------------------------------

            if (item < AuctionHouse)
            {
                SquareState& square =
                    state.squares[item];


                const PlayerNumber oldOwner =
                    square.owner;


                square.owner =
                    NobodyPlayer;

                square.offeredInTradeTo =
                    NobodyPlayer;


                messaging::sendAction(
                    actions::Type::
                        NotifySquareOwnership,
                    BankPlayer,
                    AllPlayers,
                    item,
                    NobodyPlayer,
                    oldOwner
                );
            }


            popAndRestart(state);
        }


        void tickAuction(
            GameState& state)
        {
            ++state.auction.tickCount;


            if (
                state.auction.tickCount <
                state.options.auctionGoingTimeDelay)
            {
                return;
            }


            // Original :
            // attend aussi que la queue soit vide.
            if (
                messaging::currentQueueSize() != 0)
            {
                return;
            }


            state.auction.tickCount = 0;

            ++state.auction.goingCount;


            sendFiveNumberAction(
                actions::Type::NotifyAuctionGoing,
                state.auction.highestBidder,
                state.auction.highestBid,
                state.auction
                    .propertyBeingAuctioned,
                state.auction.goingCount,
                0
            );

            if (
                sync::beginWaitOnce(
                    state,
                    actions::Type::
                        NotifyAuctionGoing
                ))
            {
                return;
            }

// WaitForEverybodyReady après "going once"
            // est volontairement différé au port réseau.


            if (
                state.auction.goingCount >= 3)
            {
                finishAuction(state);
            }
        }


        void tickHousingShortage(
            GameState& state)
        {
            // ActionTick() original :
            // synchroniser tout le monde AVANT de lancer
            // le compte à rebours de pénurie.

            if (
                state.auction.tickCount == 0 &&
                state.auction.goingCount == 0 &&
                sync::beginWaitOnce(
                    state,
                    actions::Type::
                        NotifyHousingShortage
                ))
            {
                return;
            }


            ++state.auction.tickCount;


            if (
                state.auction.tickCount <
                state.options.auctionGoingTimeDelay)
            {
                return;
            }


            state.auction.tickCount = 0;

            ++state.auction.goingCount;


            const PendingPhase shortage =
                phases::current(state);


            const PlayerNumber originalBuyer =
                shortage.fromPlayer;


            const std::uint8_t squareNo =
                shortage.toPlayer;


            const bool hotel =
                shortage.amount > 0;


            const buildings::PlayerSet allowed =
                buildings::playersWhoCanBuyBuilding(
                    state,
                    hotel
                );


            // Source :
            // s'il n'y a qu'un joueur capable d'acheter,
            // inutile d'attendre plus longtemps.
            if (
                allowed ==
                (1u << originalBuyer))
            {
                state.auction.goingCount = 3;
            }


            sendFiveNumberAction(
                actions::Type::
                    NotifyHousingShortage,
                originalBuyer,
                squareNo,
                shortage.amount,
                state.auction.goingCount,
                allowed
            );


            if (
                state.auction.goingCount < 3)
            {
                return;
            }


            // Personne d'autre n'a demandé d'enchère :
            // vente normale au demandeur initial.

            const std::int64_t price =
                board::definition(
                    static_cast<board::SquareType>(
                        squareNo
                    )
                ).housePurchaseCost;


            economy::blindlyTransferCash(
                state,
                originalBuyer,
                BankPlayer,
                price,
                true
            );


            buildings::buildApprovedBuilding(
                state,
                squareNo
            );


            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::ErrorBuildingBought,
                originalBuyer,
                price,
                squareNo
            );


            popAndRestart(state);
        }
    }


    void actionBid(
        GameState& state,
        const actions::Message& message)
    {
        if (
            phases::current(
                state
            ).phase !=
                GamePhase::Auction)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        const PlayerNumber player =
            message.fromPlayer;


        if (
            player >=
                state.numberOfPlayers)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        const buildings::PlayerSet allowed =
            playersAllowedToBid(
                state
            );


        if (
            (
                allowed &
                (1u << player)
            ) == 0)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        // Le source confirme l'action même si l'offre
        // n'est pas supérieure à l'offre actuelle.
        notifyActionCompleted(
            message,
            true
        );


        if (
            message.numberA <=
            state.auction.highestBid)
        {
            return;
        }


        state.auction.highestBidder =
            player;

        state.auction.highestBid =
            message.numberA;

        state.auction.goingCount = 0;
        state.auction.tickCount = 0;


        sendFiveNumberAction(
            actions::Type::NotifyNewHighBid,
            state.auction.highestBidder,
            state.auction.highestBid,
            state.auction
                .propertyBeingAuctioned,
            0,
            allowed
        );
    }


    void actionStartHousingAuction(
        GameState& state,
        const actions::Message& message)
    {
        if (
            phases::current(
                state
            ).phase !=
                GamePhase::
                    HousingShortageQuestion)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        const PendingPhase shortage =
            phases::current(state);


        const PlayerNumber player =
            message.fromPlayer;


        if (
            player ==
            shortage.fromPlayer)
        {
            notifyActionCompleted(
                message,
                false
            );


            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                legacy_text::
                    ErrorBuildingAlreadyBuying,
                0,
                player,
                shortage.toPlayer
            );


            return;
        }


        const bool hotel =
            shortage.amount > 0;


        const buildings::PlayerSet allowed =
            buildings::playersWhoCanBuyBuilding(
                state,
                hotel
            );


        if (
            player >= MaxPlayers ||
            (
                allowed &
                (1u << player)
            ) == 0)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        notifyActionCompleted(
            message,
            true
        );


        phases::switchTo(
            state,
            GamePhase::Auction,
            0,
            0,
            hotel
                ? AuctionHotel
                : AuctionHouse
        );


        initializeAuctionDataAndRestart(
            state
        );
    }


    bool restartAuctionPhase(
        GameState& state)
    {
        const PendingPhase phase =
            phases::current(state);


        switch (phase.phase)
        {
            case GamePhase::Auction:
            {
                sendFiveNumberAction(
                    actions::Type::
                        NotifyNewHighBid,
                    state.auction.highestBidder,
                    state.auction.highestBid,
                    state.auction
                        .propertyBeingAuctioned,
                    0,
                    playersAllowedToBid(
                        state
                    )
                );

                if (
                    sync::beginWaitOnce(
                        state,
                        actions::Type::
                            NotifyNewHighBid
                    ))
                {
                    return true;
                }


                return true;
            }


            case GamePhase::
                HousingShortageQuestion:
            {
                state.auction.tickCount = 0;
                state.auction.goingCount = 0;


                sendFiveNumberAction(
                    actions::Type::
                        NotifyHousingShortage,
                    phase.fromPlayer,
                    phase.toPlayer,
                    phase.amount,
                    0,
                    buildings::
                        playersWhoCanBuyBuilding(
                            state,
                            phase.amount > 0
                        )
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
        if (
            state.numberOfPendingPhases == 0)
        {
            return;
        }


        switch (
            phases::current(
                state
            ).phase)
        {
            case GamePhase::Auction:
                tickAuction(state);
                break;


            case GamePhase::
                HousingShortageQuestion:
                tickHousingShortage(state);
                break;


            default:
                break;
        }
    }


    bool startPendingBankPropertyAuction(
        GameState& state)
    {
        // RULE_PropertySetOwnedByPlayer(BANK)
        // puis RULE_BitSetToProperty(), qui prend
        // la première propriété dans l'ordre des cases.

        for (std::size_t squareNo = 0;
             squareNo <= 39;
             ++squareNo)
        {
            if (
                state.squares[
                    squareNo
                ].owner != BankPlayer)
            {
                continue;
            }


            if (
                !board::isOwnable(
                    static_cast<board::SquareType>(
                        squareNo
                    )
                ))
            {
                continue;
            }


            phases::push(
                state,
                GamePhase::Auction,
                0,
                0,
                squareNo
            );


            initializeAuctionDataAndRestart(
                state
            );


            return true;
        }


        return false;
    }
}


