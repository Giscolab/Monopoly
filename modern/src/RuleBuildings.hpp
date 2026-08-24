#pragma once

#include "Actions.hpp"
#include "BoardRules.hpp"
#include "RuleTypes.hpp"

#include <cstdint>

namespace monopoly::rules::buildings
{
    void resetTransientState();
    using PlayerSet = std::uint32_t;

    struct PlacementCheck
    {
        std::int64_t error = 0;
        std::int64_t errorArgument = 0;

        bool buildingAHotel = false;

        int freeBuildings = 0;
    };


    PlacementCheck testBuildingPlacement(
        const GameState& state,
        PlayerNumber purchaser,
        std::uint8_t squareNo,
        bool adding
    );


    PlayerSet playersWhoCanBuyBuilding(
        const GameState& state,
        bool hotel
    );


    board::PropertySet placeBuildingLegalSquares(
        const GameState& state
    );


    void buildApprovedBuilding(
        GameState& state,
        std::uint8_t squareNo
    );


    void actionBuyHouse(
        GameState& state,
        const actions::Message& message
    );


    void actionSellBuildings(
        GameState& state,
        const actions::Message& message
    );


    void actionCancelDecomposition(
        GameState& state,
        const actions::Message& message
    );


    void actionPlayerBuySellMortgage(
        GameState& state,
        const actions::Message& message
    );


    void actionPlayerDoneBuySellMortgage(
        GameState& state,
        const actions::Message& message
    );


    bool restartBuildingPhase(
        GameState& state
    );
}


