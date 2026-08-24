#include "CardDeckRuntime.hpp"
#include "Messaging.hpp"

#include <cstddef>
#include <cstdint>

namespace monopoly::rules::cardruntime
{
    namespace
    {
        CardDeck& getDeck(
            GameState& state,
            DeckType deck)
        {
            return state.cards[
                static_cast<std::size_t>(deck)
            ];
        }
    }


    CardType dealFromTop(
        GameState& state,
        DeckType deck)
    {
        // DealCardFromTopOfDeck() original.

        CardDeck& workingDeck =
            getDeck(state, deck);


        if (workingDeck.cardCount == 0)
        {
            return CardType::None;
        }


        const CardType card =
            static_cast<CardType>(
                workingDeck.cardPile[0]
            );


        --workingDeck.cardCount;


        // Source :
        //
        // for (i = 1; i <= cardCount; i++)
        //     pile[i - 1] = pile[i];

        for (std::uint8_t i = 1;
             i <= workingDeck.cardCount;
             ++i)
        {
            workingDeck.cardPile[
                i - 1
            ] =
                workingDeck.cardPile[i];
        }


        return card;
    }


    void returnToBottom(
        GameState& state,
        CardType card)
    {
        // ReturnCard() original.

        const std::uint8_t raw =
            static_cast<std::uint8_t>(card);


        DeckType deck{};


        if (
            raw >= ChanceFirst &&
            raw < ChanceFirst + ChanceCount)
        {
            deck = DeckType::Chance;
        }
        else if (
            raw >= CommunityFirst &&
            raw < CommunityFirst + CommunityCount)
        {
            deck = DeckType::Community;
        }
        else
        {
            return;
        }


        CardDeck& workingDeck =
            getDeck(state, deck);


        if (
            workingDeck.cardCount >=
            MaxCardsInDeck)
        {
            return;
        }


        workingDeck.cardPile[
            workingDeck.cardCount++
        ] = raw;
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
            fromPlayer = NobodyPlayer;
        }

        if (toPlayer >= MaxPlayers)
        {
            toPlayer = NobodyPlayer;
        }


        const std::size_t deckIndex =
            static_cast<std::size_t>(
                deckType
            );

        if (deckIndex >=
            static_cast<std::size_t>(
                DeckType::Count
            ))
        {
            return false;
        }


        CardDeck& deck =
            state.cards[deckIndex];


        if (deck.jailOwner != fromPlayer)
        {
            return false;
        }


        // Joueur -> paquet :
        // la carte retourne physiquement au bas du paquet.
        if (
            deck.jailOwner < MaxPlayers &&
            toPlayer == NobodyPlayer)
        {
            returnToBottom(
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
}

