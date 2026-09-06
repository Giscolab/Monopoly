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
        std::int64_t raiseCashNeeded{};
        bool raiseCashCanBankrupt{};
        rules::PlayerNumber tradeAPlayer{rules::MaxPlayers};
        rules::PlayerNumber tradeBPlayer{rules::MaxPlayers};
        bool tradeInProgress{};

        void reset() noexcept
        {
            mode = RuleMode::Nothing;
            player = rules::NobodyPlayer;
            raiseCashNeeded = 0;
            raiseCashCanBankrupt = false;
            tradeAPlayer = rules::MaxPlayers;
            tradeBPlayer = rules::MaxPlayers;
            tradeInProgress = false;
        }

        void process(const actions::Message& message) noexcept;
        void processHousingShortage(
            const actions::Message& message,
            rules::PlayerNumber resolvedPlayer) noexcept;
        void processTradeAcceptance(
            const actions::Message& message,
            rules::PlayerNumber resolvedPlayer) noexcept;
    };
}
