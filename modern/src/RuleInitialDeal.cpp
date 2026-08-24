#include "RuleInitialDeal.hpp"

#include "BoardRules.hpp"
#include "PhaseStack.hpp"
#include "RuleEconomy.hpp"
#include "RuleRandom.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace monopoly::rules::initialdeal
{
    void dealNProperties(
        GameState& state)
    {
        // ====================================================
        // DealNProperties() original.
        //
        // Plusieurs rounds.
        // Chaque round doit fournir exactement une propriété
        // à chaque joueur ou le round entier est abandonné.
        // ====================================================

        if (
            state.numberOfPlayers == 0 ||
            state.options.dealNPropertiesAtStartup == 0)
        {
            return;
        }


        std::array<
            board::SquareType,
            SquareCount
        > affordableProperties{};


        std::array<
            std::int64_t,
            MaxPlayers
        > cash{};


        std::array<
            std::int64_t,
            MaxPlayers
        > cashOwed{};


        std::array<
            board::SquareType,
            MaxPlayers
        > hypotheticalPurchase{};


        std::array<
            board::PropertySet,
            MaxPlayers
        > propertiesBought{};


        for (PlayerNumber player = 0;
             player < state.numberOfPlayers;
             ++player)
        {
            cash[player] =
                state.players[player].cash;

            cashOwed[player] = 0;

            propertiesBought[player] = 0;
        }


        int numberOfRounds = 0;


        while (
            numberOfRounds <
            state.options.dealNPropertiesAtStartup)
        {
            bool finishedFullRound =
                true;


            for (PlayerNumber player = 0;
                 player < state.numberOfPlayers;
                 ++player)
            {
                hypotheticalPurchase[player] =
                    static_cast<board::SquareType>(
                        SquareCount
                    );
            }


            for (PlayerNumber player = 0;
                 player < state.numberOfPlayers;
                 ++player)
            {
                std::size_t affordableCount = 0;


                for (std::size_t squareNo = 0;
                     squareNo < SquareCount;
                     ++squareNo)
                {
                    if (
                        state.squares[squareNo].owner !=
                            NobodyPlayer)
                    {
                        continue;
                    }


                    const auto square =
                        static_cast<board::SquareType>(
                            squareNo
                        );


                    if (!board::isOwnable(square))
                    {
                        continue;
                    }


                    bool alreadyAllocated =
                        false;


                    for (PlayerNumber other = 0;
                         other <
                            state.numberOfPlayers;
                         ++other)
                    {
                        if (
                            static_cast<std::size_t>(
                                hypotheticalPurchase[other]
                            ) ==
                            squareNo)
                        {
                            alreadyAllocated =
                                true;

                            break;
                        }
                    }


                    if (alreadyAllocated)
                    {
                        continue;
                    }


                    const auto&
                        predefined =
                        board::definition(square);


                    if (
                        !state.options
                            .dealFreePropertiesAtStartup &&
                        predefined.purchaseCost >
                            cash[player])
                    {
                        continue;
                    }


                    affordableProperties[
                        affordableCount++
                    ] = square;
                }


                if (affordableCount == 0)
                {
                    finishedFullRound =
                        false;

                    break;
                }


                const std::size_t selected =
                    static_cast<std::size_t>(
                        random::generator()()
                    ) %
                    affordableCount;


                hypotheticalPurchase[player] =
                    affordableProperties[selected];
            }


            if (!finishedFullRound)
            {
                break;
            }


            // -----------------------------------------------
            // Le round entier est valide :
            // maintenant seulement on modifie GameState.
            // -----------------------------------------------

            for (PlayerNumber player = 0;
                 player < state.numberOfPlayers;
                 ++player)
            {
                const auto square =
                    hypotheticalPurchase[player];


                const std::uint8_t squareNo =
                    static_cast<std::uint8_t>(
                        square
                    );


                economy::
                    transferPropertyForSettlement(
                        state,
                        squareNo,
                        player
                    );


                if (
                    !state.options
                        .dealFreePropertiesAtStartup)
                {
                    const std::int64_t price =
                        board::definition(
                            square
                        ).purchaseCost;


                    cashOwed[player] +=
                        price;


                    cash[player] -=
                        price;
                }


                propertiesBought[player] |=
                    board::propertyBit(
                        square
                    );
            }


            ++numberOfRounds;
        }


        // ====================================================
        // Comptabilité différée.
        //
        // Ordre conservé :
        //
        // player 0:
        //   PUSH TRANSFER
        //   PUSH DEBT
        //
        // player 1:
        //   PUSH TRANSFER
        //   PUSH DEBT
        //
        // etc.
        //
        // phaseStack étant LIFO, le dernier joueur sera
        // traité en premier, comme en 1999.
        // ====================================================

        for (PlayerNumber player = 0;
             player < state.numberOfPlayers;
             ++player)
        {
            if (
                propertiesBought[player] != 0)
            {
                phases::push(
                    state,
                    GamePhase::
                        TransferEscrowProperty,
                    0,
                    player,
                    propertiesBought[player]
                );
            }


            if (cashOwed[player] != 0)
            {
                economy::stackDebt(
                    state,
                    player,
                    BankPlayer,
                    cashOwed[player]
                );
            }
        }
    }
}
