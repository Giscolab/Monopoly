#include "RuleCards.hpp"

#include "BoardRules.hpp"
#include "CardDeckRuntime.hpp"
#include "LegacyTextIds.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"
#include "RuleEconomy.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace monopoly::rules::cards
{
    namespace
    {
        constexpr std::uint8_t GoSquare = 0;
        constexpr std::uint8_t ReadingRailroad = 5;
        constexpr std::uint8_t StCharlesPlace = 11;
        constexpr std::uint8_t ElectricCompany = 12;
        constexpr std::uint8_t PennsylvaniaRailroad = 15;
        constexpr std::uint8_t IllinoisAvenue = 24;
        constexpr std::uint8_t BAndORailroad = 25;
        constexpr std::uint8_t WaterWorks = 28;
        constexpr std::uint8_t ShortLineRailroad = 35;
        constexpr std::uint8_t Boardwalk = 39;
        constexpr std::uint8_t InJail = 40;
        constexpr std::uint8_t OffBoard = 41;


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
            const actions::Message& message)
        {
            messaging::sendAction(
                actions::Type::NotifyActionCompleted,
                BankPlayer,
                AllPlayers,
                static_cast<std::int64_t>(
                    message.action
                ),
                1,
                message.fromPlayer,
                0
            );
        }


        void sendFiveNumberMessage(
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


        void countHousesAndHotels(
            const GameState& state,
            PlayerNumber player,
            int& houses,
            int& hotels)
        {
            // RULE_CountHousesAndHotels() original.

            houses = 0;
            hotels = 0;


            for (std::size_t squareNo = 0;
                 squareNo < SquareCount;
                 ++squareNo)
            {
                const board::SquareDefinition& predefined =
                    board::definition(
                        static_cast<board::SquareType>(
                            squareNo
                        )
                    );


                if (
                    static_cast<std::size_t>(
                        predefined.group
                    ) >=
                    board::MaxPropertyGroups)
                {
                    continue;
                }


                if (
                    state.squares[
                        squareNo
                    ].owner != player)
                {
                    continue;
                }


                const int buildingCount =
                    state.squares[
                        squareNo
                    ].houses;


                if (
                    buildingCount <
                    state.options.housesPerHotel)
                {
                    houses +=
                        buildingCount;
                }
                else
                {
                    ++hotels;
                }
            }
        }


        bool transferGetOutOfJail(
            GameState& state,
            PlayerNumber fromPlayer,
            PlayerNumber toPlayer,
            DeckType deckType)
        {
            // TransferGetOutOfJail() original.

            if (fromPlayer >= MaxPlayers)
            {
                fromPlayer =
                    NobodyPlayer;
            }


            if (toPlayer >= MaxPlayers)
            {
                toPlayer =
                    NobodyPlayer;
            }


            CardDeck& deck =
                state.cards[
                    static_cast<std::size_t>(
                        deckType
                    )
                ];


            if (deck.jailOwner !=
                fromPlayer)
            {
                return false;
            }


            // Lorsqu'un joueur rend la carte au paquet,
            // elle retourne d'abord physiquement en bas.

            if (
                deck.jailOwner < MaxPlayers &&
                toPlayer == NobodyPlayer)
            {
                cardruntime::returnToBottom(
                    state,
                    deckType == DeckType::Chance
                        ? CardType::ChanceGetOutOfJailFree
                        : CardType::CommunityGetOutOfJailFree
                );
            }


            deck.jailOwner =
                toPlayer;

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


            return true;
        }


        void moveForwards(
            GameState& state,
            std::uint8_t destination)
        {
            phases::switchTo(
                state,
                GamePhase::MovingToken,
                0,
                0,
                0
            );


            messaging::sendAction(
                actions::Type::MoveForwards,
                BankPlayer,
                BankPlayer,
                destination
            );
        }


        void moveBackwards(
            GameState& state,
            std::uint8_t destination)
        {
            phases::switchTo(
                state,
                GamePhase::MovingToken,
                0,
                0,
                0
            );


            messaging::sendAction(
                actions::Type::MoveBackwards,
                BankPlayer,
                BankPlayer,
                destination
            );
        }


        void jumpTo(
            GameState& state,
            std::uint8_t destination)
        {
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
                destination
            );
        }


        void chargePropertyRepairs(
            GameState& state,
            std::int64_t perHouse,
            std::int64_t perHotel)
        {
            int houseCount = 0;
            int hotelCount = 0;


            countHousesAndHotels(
                state,
                state.currentPlayer,
                houseCount,
                hotelCount
            );


            const std::int64_t amount =
                perHouse * houseCount +
                perHotel * hotelCount;


            // End movement before debt activity stacks up.
            phases::pop(state);


            economy::stackDebtAndRestart(
                state,
                state.currentPlayer,
                BankPlayer,
                amount
            );


            economy::addMoneyToFreeParkingPot(
                state,
                amount
            );


            // MESS_SendAction2() original :
            //
            // error id,
            // amount,
            // player,
            // houseCount,
            // hotelCount

            sendFiveNumberMessage(
                actions::Type::NotifyErrorMessage,
                legacy_text::ErrorPropertyFees,
                amount,
                state.currentPlayer,
                houseCount,
                hotelCount
            );
        }


        void stackPaymentsFromOthers(
            GameState& state,
            std::int64_t amountPerPlayer)
        {
            // COMMUNITY_GET_50_FROM_EACH_PLAYER.
            //
            // L'original parcourt les joueurs EN ARRIÈRE
            // afin que l'ordre LIFO de phaseStack donne
            // l'ordre voulu au paiement.

            const PlayerNumber current =
                state.currentPlayer;


            phases::pop(state);


            PlayerNumber other =
                current;


            while (true)
            {
                if (other == 0)
                {
                    other =
                        static_cast<PlayerNumber>(
                            state.numberOfPlayers - 1
                        );
                }
                else
                {
                    --other;
                }


                if (other == current)
                {
                    break;
                }


                if (
                    state.players[
                        other
                    ].currentSquare <
                    OffBoard)
                {
                    economy::stackDebt(
                        state,
                        other,
                        current,
                        amountPerPlayer
                    );
                }
            }


            sendRestart();
        }


        void stackPaymentsToOthers(
            GameState& state,
            std::int64_t amountPerPlayer)
        {
            // CHANCE_PAY_50_TO_EACH_PLAYER.

            const PlayerNumber current =
                state.currentPlayer;


            phases::pop(state);


            PlayerNumber other =
                current;


            while (true)
            {
                if (other == 0)
                {
                    other =
                        static_cast<PlayerNumber>(
                            state.numberOfPlayers - 1
                        );
                }
                else
                {
                    --other;
                }


                if (other == current)
                {
                    break;
                }


                if (
                    state.players[
                        other
                    ].currentSquare <
                    OffBoard)
                {
                    economy::stackDebt(
                        state,
                        current,
                        other,
                        amountPerPlayer
                    );
                }
            }


            sendRestart();
        }


        DeckType deckForCurrentSquare(
            const GameState& state)
        {
            const std::uint8_t squareNo =
                state.players[
                    state.currentPlayer
                ].currentSquare;


            const board::SquareGroup group =
                board::definition(
                    static_cast<board::SquareType>(
                        squareNo
                    )
                ).group;


            return
                group ==
                    board::SquareGroup::CommunityChest
                    ? DeckType::Community
                    : DeckType::Chance;
        }
    }


    void actionCardSeen(
        GameState& state,
        const actions::Message& message)
    {
        // ====================================================
        // ActionCardSeen() original.
        // ====================================================

        if (
            phases::current(
                state
            ).phase !=
                GamePhase::WaitUntilCardSeen)
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

            return;
        }


        if (
            message.fromPlayer !=
                state.currentPlayer ||
            state.currentPlayer >=
                state.numberOfPlayers)
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

            return;
        }


        PlayerState& player =
            state.players[
                state.currentPlayer
            ];


        const DeckType deck =
            deckForCurrentSquare(
                state
            );


        const CardType pickedCard =
            cardruntime::dealFromTop(
                state,
                deck
            );


        notifyActionCompleted(
            message
        );


        messaging::sendAction(
            actions::Type::NotifyPutAwayCard,
            BankPlayer,
            AllPlayers,
            state.currentPlayer,
            static_cast<std::int64_t>(
                deck
            ),
            static_cast<std::int64_t>(
                pickedCard
            ),
            0
        );


        // Certaines cartes modifient une action qui se produit
        // après le déplacement :
        //
        //  - gare la plus proche, loyer double
        //  - compagnie la plus proche, lancer ×10

        state.pendingCard =
            pickedCard;


        switch (pickedCard)
        {
            // =================================================
            // COMMUNITY CHEST : argent reçu.
            // =================================================

            case CardType::CommunityGet100FromBank1:
            case CardType::CommunityGet100FromBank2:
            case CardType::CommunityGet100FromBank3:
            {
                economy::blindlyTransferCash(
                    state,
                    BankPlayer,
                    state.currentPlayer,
                    100,
                    true
                );

                popAndRestart(state);
                break;
            }


            case CardType::CommunityGet10FromBank:
            {
                economy::blindlyTransferCash(
                    state,
                    BankPlayer,
                    state.currentPlayer,
                    10,
                    true
                );

                popAndRestart(state);
                break;
            }


            case CardType::CommunityGet200FromBank:
            {
                economy::blindlyTransferCash(
                    state,
                    BankPlayer,
                    state.currentPlayer,
                    200,
                    true
                );

                popAndRestart(state);

                // g_Card200Counter du build non-hotseat était
                // uniquement une statistique extérieure aux règles.
                break;
            }


            case CardType::CommunityGet20FromBank:
            {
                economy::blindlyTransferCash(
                    state,
                    BankPlayer,
                    state.currentPlayer,
                    20,
                    true
                );

                popAndRestart(state);
                break;
            }


            case CardType::CommunityGet45FromBank:
            {
                economy::blindlyTransferCash(
                    state,
                    BankPlayer,
                    state.currentPlayer,
                    45,
                    true
                );

                popAndRestart(state);
                break;
            }


            case CardType::CommunityGet25FromBank:
            {
                economy::blindlyTransferCash(
                    state,
                    BankPlayer,
                    state.currentPlayer,
                    25,
                    true
                );

                popAndRestart(state);
                break;
            }


            // =================================================
            // CHANCE : argent reçu.
            // =================================================

            case CardType::ChanceGet150FromBank:
            {
                economy::blindlyTransferCash(
                    state,
                    BankPlayer,
                    state.currentPlayer,
                    150,
                    true
                );

                popAndRestart(state);
                break;
            }


            case CardType::ChanceGet50FromBank:
            {
                economy::blindlyTransferCash(
                    state,
                    BankPlayer,
                    state.currentPlayer,
                    50,
                    true
                );

                popAndRestart(state);

                // g_Card50Counter idem : statistique externe.
                break;
            }


            // =================================================
            // GET OUT OF JAIL FREE.
            // =================================================

            case CardType::ChanceGetOutOfJailFree:
            case CardType::CommunityGetOutOfJailFree:
            {
                transferGetOutOfJail(
                    state,
                    NobodyPlayer,
                    state.currentPlayer,
                    deck
                );

                popAndRestart(state);
                break;
            }


            // =================================================
            // Paiements à la banque.
            // =================================================

            case CardType::CommunityPay50ToBank:
            {
                phases::pop(state);

                economy::stackDebtAndRestart(
                    state,
                    state.currentPlayer,
                    BankPlayer,
                    50
                );

                economy::addMoneyToFreeParkingPot(
                    state,
                    50
                );

                break;
            }


            case CardType::CommunityPay150ToBank:
            {
                phases::pop(state);

                economy::stackDebtAndRestart(
                    state,
                    state.currentPlayer,
                    BankPlayer,
                    150
                );

                economy::addMoneyToFreeParkingPot(
                    state,
                    150
                );

                break;
            }


            case CardType::CommunityPay100ToBank:
            {
                phases::pop(state);

                economy::stackDebtAndRestart(
                    state,
                    state.currentPlayer,
                    BankPlayer,
                    100
                );

                economy::addMoneyToFreeParkingPot(
                    state,
                    100
                );

                break;
            }


            case CardType::ChancePay15ToBank:
            {
                phases::pop(state);

                economy::stackDebtAndRestart(
                    state,
                    state.currentPlayer,
                    BankPlayer,
                    15
                );

                economy::addMoneyToFreeParkingPot(
                    state,
                    15
                );

                break;
            }


            // =================================================
            // Réparations propriétés.
            // =================================================

            case CardType::
                CommunityPay40EachHouse115EachHotel:
            {
                chargePropertyRepairs(
                    state,
                    40,
                    115
                );

                break;
            }


            case CardType::
                ChancePay25EachHouse100EachHotel:
            {
                chargePropertyRepairs(
                    state,
                    25,
                    100
                );

                break;
            }


            // =================================================
            // Paiements entre joueurs.
            // =================================================

            case CardType::
                CommunityGet50FromEachPlayer:
            {
                stackPaymentsFromOthers(
                    state,
                    50
                );

                break;
            }


            case CardType::
                ChancePay50ToEachPlayer:
            {
                stackPaymentsToOthers(
                    state,
                    50
                );

                break;
            }


            // =================================================
            // Prison.
            // =================================================

            case CardType::
                CommunityGoDirectlyToJail:

            case CardType::
                ChanceGoDirectlyToJail:
            {
                jumpTo(
                    state,
                    InJail
                );

                break;
            }


            // =================================================
            // GO.
            // =================================================

            case CardType::
                CommunityGoDirectlyToGo:

            case CardType::
                ChanceGoDirectlyToGo:
            {
                jumpTo(
                    state,
                    GoSquare
                );

                break;
            }


            // =================================================
            // Déplacements Chance.
            // =================================================

            case CardType::
                ChanceGoToReadingRailroad:
            {
                moveForwards(
                    state,
                    ReadingRailroad
                );

                break;
            }


            case CardType::
                ChanceGoToStCharlesPlace:
            {
                moveForwards(
                    state,
                    StCharlesPlace
                );

                break;
            }


            case CardType::
                ChanceGoToBoardwalk:
            {
                moveForwards(
                    state,
                    Boardwalk
                );

                break;
            }


            case CardType::
                ChanceGoToIllinoisAvenue:
            {
                moveForwards(
                    state,
                    IllinoisAvenue
                );

                break;
            }


            // =================================================
            // Compagnie la plus proche.
            // =================================================

            case CardType::
                ChanceGoToNearestUtility:
            {
                std::uint8_t newSquare = 0;


                if (
                    player.currentSquare >=
                        ElectricCompany &&
                    player.currentSquare <
                        WaterWorks)
                {
                    newSquare =
                        WaterWorks;
                }
                else
                {
                    newSquare =
                        ElectricCompany;
                }


                moveForwards(
                    state,
                    newSquare
                );

                break;
            }


            // =================================================
            // Gare la plus proche + loyer double.
            // =================================================

            case CardType::
                ChanceGoToNearestRailroadPayDouble1:

            case CardType::
                ChanceGoToNearestRailroadPayDouble2:
            {
                std::uint8_t newSquare = 0;


                if (
                    player.currentSquare >=
                        ShortLineRailroad ||
                    player.currentSquare <
                        ReadingRailroad)
                {
                    newSquare =
                        ReadingRailroad;
                }
                else if (
                    player.currentSquare <
                        PennsylvaniaRailroad)
                {
                    newSquare =
                        PennsylvaniaRailroad;
                }
                else if (
                    player.currentSquare <
                        BAndORailroad)
                {
                    newSquare =
                        BAndORailroad;
                }
                else
                {
                    newSquare =
                        ShortLineRailroad;
                }


                moveForwards(
                    state,
                    newSquare
                );

                break;
            }


            // =================================================
            // Reculez de trois cases.
            // =================================================

            case CardType::
                ChanceGoBackThreeSpaces:
            {
                int newSquare =
                    static_cast<int>(
                        player.currentSquare
                    ) - 3;


                if (newSquare < 0)
                {
                    newSquare +=
                        Boardwalk + 1;
                }


                moveBackwards(
                    state,
                    static_cast<std::uint8_t>(
                        newSquare
                    )
                );

                break;
            }


            // =================================================
            // Erreur de paquet / carte inconnue.
            // =================================================

            case CardType::None:
            default:
            {
                messaging::sendAction(
                    actions::Type::NotifyErrorMessage,
                    BankPlayer,
                    AllPlayers,
                    legacy_text::ErrorUnimplementedCard,
                    static_cast<std::int64_t>(
                        pickedCard
                    ),
                    state.currentPlayer,
                    player.currentSquare
                );


                popAndRestart(state);
                break;
            }
        }


        // ====================================================
        // ReturnCard() original.
        //
        // Toutes les cartes reviennent en bas du paquet SAUF
        // les deux cartes "Get Out of Jail Free", qui restent
        // hors paquet tant qu'un joueur les possède.
        // ====================================================

        if (
            pickedCard !=
                CardType::ChanceGetOutOfJailFree &&
            pickedCard !=
                CardType::CommunityGetOutOfJailFree)
        {
            cardruntime::returnToBottom(
                state,
                pickedCard
            );
        }
    }
}
