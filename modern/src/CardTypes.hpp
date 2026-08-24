#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace monopoly::rules
{
    enum class CardType : std::uint8_t
    {
        None = 0,

        ChanceGoDirectlyToGo = 1,
        ChanceGoToStCharlesPlace,
        ChanceGoToNearestUtility,
        ChanceGet150FromBank,
        ChanceGet50FromBank,
        ChancePay50ToEachPlayer,
        ChanceGoToBoardwalk,
        ChanceGoToReadingRailroad,
        ChanceGoToNearestRailroadPayDouble1,
        ChanceGoToNearestRailroadPayDouble2,
        ChancePay25EachHouse100EachHotel,
        ChanceGetOutOfJailFree,
        ChancePay15ToBank,
        ChanceGoToIllinoisAvenue,
        ChanceGoDirectlyToJail,
        ChanceGoBackThreeSpaces,

        CommunityGet100FromBank1 = 65,
        CommunityGet100FromBank2,
        CommunityGet100FromBank3,
        CommunityGet10FromBank,
        CommunityGetOutOfJailFree,
        CommunityPay50ToBank,
        CommunityGet200FromBank,
        CommunityGet20FromBank,
        CommunityPay40EachHouse115EachHotel,
        CommunityGoDirectlyToJail,
        CommunityGet45FromBank,
        CommunityGet50FromEachPlayer,
        CommunityGoDirectlyToGo,
        CommunityPay150ToBank,
        CommunityGet25FromBank,
        CommunityPay100ToBank
    };

    inline constexpr std::uint8_t ChanceFirst = 1;
    inline constexpr std::uint8_t ChanceCount = 16;

    inline constexpr std::uint8_t CommunityFirst = 65;
    inline constexpr std::uint8_t CommunityCount = 16;

    inline constexpr std::size_t MaxCardsInDeck = 16;

    enum class DeckType : std::uint8_t
    {
        Chance = 0,
        Community,
        Count
    };

    struct CardDeck
    {
        std::uint8_t cardCount = 0;

        std::array<std::uint8_t, MaxCardsInDeck>
            cardPile{};

        std::uint8_t jailOwner = 7;
        std::uint8_t jailOfferedInTradeTo = 7;
    };
}
