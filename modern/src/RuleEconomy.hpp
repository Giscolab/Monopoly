#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

namespace monopoly::rules::economy
{
    void resetTransientState();
    void sellAllBuildingsForSettlement(
        GameState& state,
        std::uint8_t squareNo
    );

    void transferPropertyForSettlement(
        GameState& state,
        std::uint8_t squareNo,
        PlayerNumber toPlayer
    );

    void blindlyTransferCash(
        GameState& state,
        PlayerNumber fromPlayer,
        PlayerNumber toPlayer,
        std::int64_t amount,
        bool sendAnimation
    );

    void addMoneyToFreeParkingPot(
        GameState& state,
        std::int64_t amount
    );

    void stackDebt(
        GameState& state,
        PlayerNumber fromPlayer,
        PlayerNumber toPlayer,
        std::int64_t amount
    );

    void stackDebtAndRestart(
        GameState& state,
        PlayerNumber fromPlayer,
        PlayerNumber toPlayer,
        std::int64_t amount
    );

    void collectRent(GameState& state);

    void actionBuyOrAuctionDecision(
        GameState& state,
        const actions::Message& message
    );

    void actionTaxDecision(
        GameState& state,
        const actions::Message& message
    );

    void actionFreeUnmortgageDone(
        GameState& state,
        const actions::Message& message
    );

    void actionMortgaging(
        GameState& state,
        const actions::Message& message
    );

    void actionSellBuildings(
        GameState& state,
        const actions::Message& message
    );

    void actionGoBankrupt(
        GameState& state,
        const actions::Message& message
    );

    void landOnIncomeTax(GameState& state);
    void landOnLuxuryTax(GameState& state);
    void landOnFreeParking(GameState& state);

    bool restartEconomyPhase(
        GameState& state,
        const actions::Message& message
    );
}





