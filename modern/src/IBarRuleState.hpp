#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

#include <cstdint>

namespace monopoly::ibar
{
    // Source/monopoly/UDIBar.h: enum IBAR_STATES, numeric order preserved.
    enum class RuleMode : std::uint8_t
    {
        Nothing = 0,
        StartTurn,
        Build,
        Sell,
        Mortgage,
        UnMortgage,
        OtherPlayer,
        RaiseMoney,
        DoneTurn,
        DeedActive,
        OtherPlayerRemote,
        BuyAuction,
        ViewingCard,
        JailExitPCR,
        JailExitPXR,
        JailExitPCX,
        JailExitPXX,
        Trading,
        FreeUnmortgage,
        TaxDecision,
        HousingShort,
        HotelShort,
        PlaceHouse,
        PlaceHotel,
        HotelDecomposition,
        GameOver,
        StatusScreen,
        Max
    };

    struct RuleProjection
    {
        RuleMode mode{RuleMode::Nothing};
        rules::PlayerNumber player{rules::NobodyPlayer};

        void reset() noexcept
        {
            mode = RuleMode::Nothing;
            player = rules::NobodyPlayer;
        }

        void process(const actions::Message& message) noexcept;
    };
}
