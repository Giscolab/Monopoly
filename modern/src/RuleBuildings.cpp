#include "RuleBuildings.hpp"

#include "LegacyTextIds.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"
#include "RuleEconomy.hpp"
#include "RuleSynchronization.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace monopoly::rules::buildings
{
    namespace
    {
        constexpr std::uint8_t BoardwalkSquare = 39;
        constexpr std::uint8_t OffBoardSquare = 41;

        std::optional<GameState>
            hotelDecompositionSnapshot;


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
                actions::Type::NotifyActionCompleted,
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


        void notifyError(
            std::int64_t error,
            std::int64_t argument,
            PlayerNumber player,
            std::uint8_t square)
        {
            messaging::sendAction(
                actions::Type::NotifyErrorMessage,
                BankPlayer,
                AllPlayers,
                error,
                argument,
                player,
                square
            );
        }


        bool quickBuyAllowed(
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
                case GamePhase::PlaceBuilding:
                    return true;

                default:
                    return false;
            }
        }


        bool quickSellAllowed(
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


        void sellAllBuildingsOnMonopoly(
            GameState& state,
            std::uint8_t squareNo)
        {
            const board::SquareGroup group =
                board::definition(
                    static_cast<board::SquareType>(
                        squareNo
                    )
                ).group;

            const PlayerNumber player =
                state.squares[
                    squareNo
                ].owner;

            std::int64_t cash = 0;


            for (std::size_t i = 0;
                 i < SquareCount;
                 ++i)
            {
                if (
                    board::definition(
                        static_cast<board::SquareType>(
                            i
                        )
                    ).group != group)
                {
                    continue;
                }


                const std::uint8_t houses =
                    state.squares[i].houses;

                state.squares[i].houses = 0;


                if (houses == 0)
                {
                    continue;
                }


                messaging::sendAction(
                    actions::Type::NotifySquareHouses,
                    BankPlayer,
                    AllPlayers,
                    i,
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
                                i
                            )
                        ).housePurchaseCost
                        + 1
                    ) / 2;
            }


            if (cash == 0)
            {
                return;
            }


            economy::blindlyTransferCash(
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
                squareNo
            );
        }


        int housesCurrentlyOnBoard(
            const GameState& state)
        {
            int houseCount = 0;

            for (std::size_t i = 0;
                 i < SquareCount;
                 ++i)
            {
                const int houses =
                    state.squares[i].houses;

                if (
                    houses <
                    state.options.housesPerHotel)
                {
                    houseCount += houses;
                }
            }

            return houseCount;
        }
    }


    void resetTransientState()
    {
        hotelDecompositionSnapshot.reset();
    }


    PlacementCheck testBuildingPlacement(
        const GameState& state,
        PlayerNumber purchaser,
        std::uint8_t squareNo,
        bool adding)
    {
        PlacementCheck result{};


        if (squareNo >= SquareCount ||
            purchaser >= state.numberOfPlayers)
        {
            result.error =
                legacy_text::ErrorBuildingOnNonProperty;

            return result;
        }


        const GamePhase currentPhase =
            state.numberOfPendingPhases == 0
                ? GamePhase::AddingNewPlayers
                : state.phaseStack[0].phase;


        const bool noCharge =
            currentPhase ==
                GamePhase::PlaceBuilding ||
            currentPhase ==
                GamePhase::Auction ||
            currentPhase ==
                GamePhase::HousingShortageQuestion;


        const board::SquareDefinition& predefined =
            board::definition(
                static_cast<board::SquareType>(
                    squareNo
                )
            );


        const board::SquareGroup monopolyGroup =
            predefined.group;


        // ----------------------------------------------------
        // Cash.
        // ----------------------------------------------------

        if (adding && !noCharge)
        {
            if (
                predefined.housePurchaseCost >
                state.players[
                    purchaser
                ].cash)
            {
                result.error =
                    legacy_text::ErrorBuildingNeedsCash;

                result.errorArgument =
                    predefined.housePurchaseCost;

                return result;
            }
        }


        // ----------------------------------------------------
        // Terrain constructible.
        // ----------------------------------------------------

        if (
            static_cast<std::uint8_t>(
                monopolyGroup
            ) >
            static_cast<std::uint8_t>(
                board::SquareGroup::ParkPlace
            ))
        {
            result.error =
                legacy_text::ErrorBuildingOnNonProperty;

            return result;
        }


        const SquareState& square =
            state.squares[
                squareNo
            ];


        if (square.owner != purchaser)
        {
            result.error =
                legacy_text::ErrorBuildingOnUnowned;

            return result;
        }


        // ----------------------------------------------------
        // Possession du groupe entier + aucune hypothèque.
        // ----------------------------------------------------

        int lowestHouses =
            state.options.housesPerHotel;

        int highestHouses = 0;


        for (std::size_t i = 0;
             i < SquareCount;
             ++i)
        {
            if (
                board::definition(
                    static_cast<board::SquareType>(
                        i
                    )
                ).group != monopolyGroup)
            {
                continue;
            }


            const SquareState& testSquare =
                state.squares[i];


            if (testSquare.owner != purchaser)
            {
                result.error =
                    legacy_text::ErrorBuildingNeedsMonopoly;

                return result;
            }


            if (testSquare.mortgaged)
            {
                result.error =
                    legacy_text::ErrorBuildingMortgaged;

                return result;
            }


            lowestHouses =
                std::min(
                    lowestHouses,
                    static_cast<int>(
                        testSquare.houses
                    )
                );


            highestHouses =
                std::max(
                    highestHouses,
                    static_cast<int>(
                        testSquare.houses
                    )
                );
        }


        if (
            adding &&
            square.houses >=
                state.options.housesPerHotel)
        {
            result.error =
                legacy_text::ErrorBuildingMaxedOut;

            return result;
        }


        // ----------------------------------------------------
        // Inventaire banque : 32 maisons / 12 hôtels par
        // défaut.
        // ----------------------------------------------------

        int houseCount = 0;
        int hotelCount = 0;


        for (std::size_t i = 0;
             i < SquareCount;
             ++i)
        {
            const int houses =
                state.squares[i].houses;


            if (
                houses <
                state.options.housesPerHotel)
            {
                houseCount += houses;
            }
            else
            {
                ++hotelCount;
            }
        }


        const int freeHouses =
            static_cast<int>(
                state.options.maximumHouses
            ) -
            houseCount;


        const int freeHotels =
            static_cast<int>(
                state.options.maximumHotels
            ) -
            hotelCount;


        // ----------------------------------------------------
        // Ajouter.
        // ----------------------------------------------------

        if (adding)
        {
            if (
                state.options.evenBuildRule &&
                square.houses >
                    lowestHouses)
            {
                result.error =
                    legacy_text::ErrorBuildingNotEven;

                result.errorArgument =
                    lowestHouses;

                return result;
            }


            if (
                square.houses >=
                state.options.housesPerHotel - 1)
            {
                result.buildingAHotel = true;

                if (freeHotels < 1)
                {
                    result.error =
                        legacy_text::ErrorBuildingNoHotels;

                    return result;
                }


                result.freeBuildings =
                    freeHotels;
            }
            else
            {
                result.buildingAHotel = false;

                if (freeHouses < 1)
                {
                    result.error =
                        legacy_text::ErrorBuildingNoHouses;

                    return result;
                }


                result.freeBuildings =
                    freeHouses;
            }
        }

        // ----------------------------------------------------
        // Retirer.
        // ----------------------------------------------------

        else
        {
            if (
                square.houses >=
                state.options.housesPerHotel)
            {
                result.buildingAHotel = true;


                if (
                    currentPhase !=
                        GamePhase::DecomposeHotel &&
                    freeHouses <
                        state.options.housesPerHotel - 1)
                {
                    result.error =
                        legacy_text::
                            ErrorBuildingNoBreakdownHouses;

                    result.errorArgument =
                        state.options.housesPerHotel - 1;

                    return result;
                }


                result.freeBuildings =
                    freeHotels;
            }
            else
            {
                result.buildingAHotel = false;


                if (square.houses <= 0)
                {
                    result.error =
                        legacy_text::ErrorSellNoHouses;

                    return result;
                }


                if (
                    state.options.evenBuildRule &&
                    square.houses <
                        highestHouses)
                {
                    result.error =
                        legacy_text::ErrorBuildingNotEven;

                    result.errorArgument =
                        highestHouses;

                    return result;
                }


                result.freeBuildings =
                    freeHouses;
            }
        }


        return result;
    }


    PlayerSet playersWhoCanBuyBuilding(
        const GameState& state,
        bool hotel)
    {
        PlayerSet allowed = 0;


        for (std::size_t squareNo = 0;
             squareNo < SquareCount;
             ++squareNo)
        {
            const PlayerNumber player =
                state.squares[
                    squareNo
                ].owner;


            if (player >= MaxPlayers)
            {
                continue;
            }


            if (
                (
                    allowed &
                    (1u << player)
                ) != 0)
            {
                continue;
            }


            const PlacementCheck check =
                testBuildingPlacement(
                    state,
                    player,
                    static_cast<std::uint8_t>(
                        squareNo
                    ),
                    true
                );


            if (
                check.error == 0 &&
                check.buildingAHotel == hotel)
            {
                allowed |=
                    (1u << player);
            }
        }


        return allowed;
    }


    board::PropertySet placeBuildingLegalSquares(
        const GameState& state)
    {
        if (
            state.numberOfPendingPhases == 0 ||
            state.phaseStack[0].phase !=
                GamePhase::PlaceBuilding)
        {
            return 0;
        }


        board::PropertySet legalSet = 0;


        for (std::size_t squareNo = 0;
             squareNo < SquareCount;
             ++squareNo)
        {
            const PlacementCheck check =
                testBuildingPlacement(
                    state,
                    state.phaseStack[0].fromPlayer,
                    static_cast<std::uint8_t>(
                        squareNo
                    ),
                    true
                );


            if (check.error != 0)
            {
                continue;
            }


            const bool wantsHotel =
                state.phaseStack[0].amount > 0;


            if (
                check.buildingAHotel ==
                wantsHotel)
            {
                legalSet |=
                    board::propertyBit(
                        static_cast<board::SquareType>(
                            squareNo
                        )
                    );
            }
        }


        return legalSet;
    }


    void buildApprovedBuilding(
        GameState& state,
        std::uint8_t squareNo)
    {
        // BuildHouse() original.

        ++state.squares[
            squareNo
        ].houses;


        messaging::sendAction(
            actions::Type::NotifySquareHouses,
            BankPlayer,
            AllPlayers,
            squareNo,
            state.squares[
                squareNo
            ].houses,
            state.options.housesPerHotel
        );
    }


    void actionBuyHouse(
        GameState& state,
        const actions::Message& message)
    {
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


        const std::uint8_t squareNo =
            static_cast<std::uint8_t>(
                message.numberA
            );


        const GamePhase phase =
            phases::current(
                state
            ).phase;


        const bool quick =
            message.numberD != 0;


        if (quick)
        {
            if (!quickBuyAllowed(phase))
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
                    GamePhase::PlaceBuilding)
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
                    GamePhase::PlaceBuilding &&
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
        }


        const PlacementCheck check =
            testBuildingPlacement(
                state,
                player,
                squareNo,
                true
            );


        if (check.error != 0)
        {
            notifyActionCompleted(
                message,
                false
            );


            notifyError(
                check.error,
                check.errorArgument,
                player,
                squareNo
            );

            return;
        }


        // ----------------------------------------------------
        // Bâtiment déjà acheté aux enchères :
        // il faut placer le bon type.
        // ----------------------------------------------------

        if (
            phase ==
                GamePhase::PlaceBuilding &&
            phases::current(
                state
            ).amount !=
                (
                    check.buildingAHotel
                        ? 1
                        : -1
                ))
        {
            notifyActionCompleted(
                message,
                false
            );


            notifyError(
                legacy_text::
                    ErrorAuctionWrongBuilding,
                0,
                player,
                squareNo
            );

            return;
        }


        const PlayerSet allowed =
            playersWhoCanBuyBuilding(
                state,
                check.buildingAHotel
            );


        const int shortageLevel =
            check.buildingAHotel
                ? state.options.hotelShortageLevel
                : state.options.houseShortageLevel;


        // ----------------------------------------------------
        // Pénurie.
        // ----------------------------------------------------

        if (
            allowed !=
                (1u << player) &&
            phase !=
                GamePhase::PlaceBuilding &&
            check.freeBuildings <=
                shortageLevel)
        {
            notifyActionCompleted(
                message,
                false
            );


            if (
                phase ==
                    GamePhase::BuySellMortgage)
            {
                messaging::sendAction(
                    actions::Type::
                        NotifyPlayerBuySellMort,
                    BankPlayer,
                    AllPlayers,
                    NobodyPlayer,
                    0,
                    0,
                    NobodyPlayer
                );
            }


            phases::push(
                state,
                GamePhase::
                    HousingShortageQuestion,
                player,
                squareNo,
                check.buildingAHotel
                    ? check.freeBuildings
                    : -check.freeBuildings
            );


            state.auction.tickCount = 0;
            state.auction.goingCount = 0;

            // ActionBuyHouse() original :
            // IgnoreWaitForEverybodyReady = FALSE.
            sync::resetWaitGate();


            sendRestart();

            return;
        }


        // ----------------------------------------------------
        // Achat normal.
        // ----------------------------------------------------

        notifyActionCompleted(
            message,
            true
        );


        if (
            phase !=
            GamePhase::PlaceBuilding)
        {
            economy::blindlyTransferCash(
                state,
                player,
                BankPlayer,
                board::definition(
                    static_cast<board::SquareType>(
                        squareNo
                    )
                ).housePurchaseCost,
                true
            );
        }


        buildApprovedBuilding(
            state,
            squareNo
        );


        messaging::sendAction(
            actions::Type::NotifyErrorMessage,
            BankPlayer,
            AllPlayers,
            legacy_text::ErrorBuildingBought,
            player,
            phase ==
                GamePhase::PlaceBuilding
                ? 0
                : board::definition(
                    static_cast<board::SquareType>(
                        squareNo
                    )
                ).housePurchaseCost,
            squareNo
        );


        if (
            phase ==
                GamePhase::PlaceBuilding)
        {
            popAndRestart(state);
        }
    }


    void actionSellBuildings(
        GameState& state,
        const actions::Message& message)
    {
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


        const std::uint8_t squareNo =
            static_cast<std::uint8_t>(
                message.numberA
            );


        const GamePhase phaseBefore =
            phases::current(
                state
            ).phase;


        const bool quick =
            message.numberD != 0;


        if (quick)
        {
            if (
                !quickSellAllowed(
                    phaseBefore
                ))
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
                phaseBefore !=
                    GamePhase::BuySellMortgage &&
                phaseBefore !=
                    GamePhase::DecomposeHotel)
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
        }


        // ----------------------------------------------------
        // Vente de tout le groupe.
        // ----------------------------------------------------

        if (message.numberB != 0)
        {
            if (
                state.squares[
                    squareNo
                ].owner != player)
            {
                notifyActionCompleted(
                    message,
                    false
                );


                notifyError(
                    legacy_text::
                        ErrorBuildingOnUnowned,
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
                squareNo
            );


            if (
                quick &&
                phaseBefore ==
                    GamePhase::CollectingPayment)
            {
                sendRestart();
            }


            return;
        }


        // ----------------------------------------------------
        // Vente d'un seul bâtiment.
        // ----------------------------------------------------

        PlacementCheck check =
            testBuildingPlacement(
                state,
                player,
                squareNo,
                false
            );


        if (
            check.error ==
                legacy_text::
                    ErrorBuildingNoBreakdownHouses &&
            phaseBefore !=
                GamePhase::DecomposeHotel)
        {
            // Original :
            //
            // Push GF_DECOMPOSE_HOTEL,
            // SaveGameStateInCurrentPhase(),
            // puis la vente de l'hôtel continue quand même.

            if (
                phaseBefore ==
                    GamePhase::BuySellMortgage)
            {
                messaging::sendAction(
                    actions::Type::
                        NotifyPlayerBuySellMort,
                    BankPlayer,
                    AllPlayers,
                    NobodyPlayer,
                    0,
                    0,
                    NobodyPlayer
                );
            }


            phases::push(
                state,
                GamePhase::DecomposeHotel,
                player,
                NobodyPlayer,
                squareNo
            );


            hotelDecompositionSnapshot =
                state;


            sendRestart();


            // Dans GF_DECOMPOSE_HOTEL le manque de maisons
            // n'empêche plus la vente.
            check.error = 0;
        }


        if (check.error != 0)
        {
            notifyActionCompleted(
                message,
                false
            );


            notifyError(
                check.error,
                check.errorArgument,
                player,
                squareNo
            );

            return;
        }


        if (
            state.squares[
                squareNo
            ].houses == 0)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        --state.squares[
            squareNo
        ].houses;


        notifyActionCompleted(
            message,
            true
        );


        const std::int64_t salePrice =
            (
                board::definition(
                    static_cast<board::SquareType>(
                        squareNo
                    )
                ).housePurchaseCost +
                1
            ) / 2;


        economy::blindlyTransferCash(
            state,
            BankPlayer,
            player,
            salePrice,
            true
        );


        messaging::sendAction(
            actions::Type::NotifySquareHouses,
            BankPlayer,
            AllPlayers,
            squareNo,
            state.squares[
                squareNo
            ].houses,
            state.options.housesPerHotel
        );


        messaging::sendAction(
            actions::Type::NotifyErrorMessage,
            BankPlayer,
            AllPlayers,
            legacy_text::ErrorBuildingSold,
            player,
            salePrice,
            squareNo
        );


        // ----------------------------------------------------
        // Décomposition hôtel.
        // ----------------------------------------------------

        if (
            phases::current(
                state
            ).phase ==
                GamePhase::DecomposeHotel)
        {
            const int houseCount =
                housesCurrentlyOnBoard(
                    state
                );


            if (
                houseCount <=
                state.options.maximumHouses)
            {
                hotelDecompositionSnapshot.reset();

                popAndRestart(state);
            }
            else
            {
                sendRestart();
            }
        }


        if (
            quick &&
            phases::current(
                state
            ).phase ==
                GamePhase::CollectingPayment)
        {
            sendRestart();
        }
    }


    void actionCancelDecomposition(
        GameState& state,
        const actions::Message& message)
    {
        if (
            phases::current(
                state
            ).phase !=
                GamePhase::DecomposeHotel)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        if (
            message.fromPlayer !=
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


        notifyActionCompleted(
            message,
            true
        );


        if (
            hotelDecompositionSnapshot
                .has_value())
        {
            const std::uint64_t duration =
                state.gameDurationInSeconds;


            state =
                *hotelDecompositionSnapshot;


            // La restauration originale retransmet l'état,
            // mais le temps de partie ne doit pas reculer
            // pendant le dialogue utilisateur.
            state.gameDurationInSeconds =
                duration;


            hotelDecompositionSnapshot.reset();
        }


        popAndRestart(state);
    }


    void actionPlayerBuySellMortgage(
        GameState& state,
        const actions::Message& message)
    {
        // ActionPlayerBuySellMort().

        const PlayerNumber player =
            message.fromPlayer;


        if (
            phases::current(
                state
            ).phase ==
                GamePhase::BuySellMortgage)
        {
            if (
                player ==
                phases::current(
                    state
                ).fromPlayer)
            {
                state.phaseStack[0].amount =
                    message.numberB;


                notifyActionCompleted(
                    message,
                    true
                );
            }
            else
            {
                notifyActionCompleted(
                    message,
                    false
                );
            }


            sendRestart();
            return;
        }


        if (
            player >=
                state.numberOfPlayers ||
            state.players[
                player
            ].currentSquare >=
                OffBoardSquare)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        const GamePhase phase =
            phases::current(
                state
            ).phase;


        bool allowed = false;


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
                allowed = true;
                break;

            default:
                break;
        }


        if (!allowed)
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


        phases::push(
            state,
            GamePhase::BuySellMortgage,
            player,
            NobodyPlayer,
            message.numberB
        );


        sendRestart();
    }


    void actionPlayerDoneBuySellMortgage(
        GameState& state,
        const actions::Message& message)
    {
        if (
            phases::current(
                state
            ).phase !=
                GamePhase::BuySellMortgage)
        {
            notifyActionCompleted(
                message,
                false
            );

            return;
        }


        if (
            message.fromPlayer !=
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


        notifyActionCompleted(
            message,
            true
        );


        messaging::sendAction(
            actions::Type::
                NotifyPlayerBuySellMort,
            BankPlayer,
            AllPlayers,
            NobodyPlayer,
            0,
            0,
            NobodyPlayer
        );


        popAndRestart(state);
    }


    bool restartBuildingPhase(
        GameState& state)
    {
        const PendingPhase phase =
            phases::current(
                state
            );


        switch (phase.phase)
        {
            case GamePhase::BuySellMortgage:
            {
                const bool bankrupt =
                    phase.fromPlayer >=
                        state.numberOfPlayers ||
                    state.players[
                        phase.fromPlayer
                    ].currentSquare >=
                        OffBoardSquare;


                if (bankrupt)
                {
                    messaging::sendAction(
                        actions::Type::
                            NotifyPlayerBuySellMort,
                        BankPlayer,
                        AllPlayers,
                        NobodyPlayer,
                        0,
                        0,
                        NobodyPlayer
                    );


                    popAndRestart(state);
                    return true;
                }


                std::int64_t cashOwing = 0;

                PlayerNumber owedTo =
                    NobodyPlayer;


                if (
                    state.numberOfPendingPhases >= 2 &&
                    state.phaseStack[1].phase ==
                        GamePhase::CollectingPayment &&
                    state.phaseStack[1].fromPlayer ==
                        phase.fromPlayer)
                {
                    cashOwing =
                        state.phaseStack[1].amount;

                    owedTo =
                        state.phaseStack[1].toPlayer;
                }


                messaging::sendAction(
                    actions::Type::
                        NotifyPlayerBuySellMort,
                    BankPlayer,
                    AllPlayers,
                    phase.fromPlayer,
                    phase.amount,
                    cashOwing,
                    owedTo
                );


                return true;
            }


            case GamePhase::DecomposeHotel:
            {
                const int count =
                    housesCurrentlyOnBoard(
                        state
                    );


                messaging::sendAction(
                    actions::Type::
                        NotifyDecomposeSale,
                    BankPlayer,
                    AllPlayers,
                    phase.fromPlayer,
                    count -
                        state.options.maximumHouses
                );


                return true;
            }


            case GamePhase::PlaceBuilding:
            {
                const bool bankrupt =
                    phase.fromPlayer >=
                        state.numberOfPlayers ||
                    state.players[
                        phase.fromPlayer
                    ].currentSquare >=
                        OffBoardSquare;


                const board::PropertySet legal =
                    placeBuildingLegalSquares(
                        state
                    );


                if (bankrupt ||
                    legal == 0)
                {
                    popAndRestart(state);
                    return true;
                }


                messaging::sendAction(
                    actions::Type::
                        NotifyPlaceBuilding,
                    BankPlayer,
                    AllPlayers,
                    phase.fromPlayer,
                    phase.amount,
                    legal
                );


                return true;
            }


            default:
                return false;
        }
    }
}


