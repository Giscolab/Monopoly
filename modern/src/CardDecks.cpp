#include "CardDecks.hpp"

#include <cstddef>
#include <utility>

namespace monopoly::rules::cards
{
    namespace
    {
        void shuffleCardDeck(
            CardDeck& deck,
            std::uint8_t firstCardOffset,
            std::mt19937& randomGenerator)
        {
            // ShuffleCardDeck() original :
            //
            // 1. cartes dans l'ordre
            // 2. pour chaque carte, échange avec rand()%count.

            for (std::uint8_t i = 0;
                 i < deck.cardCount;
                 ++i)
            {
                deck.cardPile[i] =
                    static_cast<std::uint8_t>(
                        i + firstCardOffset
                    );
            }

            for (std::uint8_t i = 0;
                 i < deck.cardCount;
                 ++i)
            {
                const std::size_t randomIndex =
                    randomGenerator() %
                    deck.cardCount;

                std::swap(
                    deck.cardPile[i],
                    deck.cardPile[randomIndex]
                );
            }
        }
    }


    void initializeDecks(
        GameState& state,
        std::mt19937& randomGenerator)
    {
        // L'original initialise Community Chest EN PREMIER.

        CardDeck& community =
            state.cards[
                static_cast<std::size_t>(
                    DeckType::Community
                )
            ];

        community = {};
        community.cardCount = CommunityCount;
        community.jailOwner = NobodyPlayer;
        community.jailOfferedInTradeTo =
            NobodyPlayer;

        shuffleCardDeck(
            community,
            CommunityFirst,
            randomGenerator
        );


        // Puis Chance.

        CardDeck& chance =
            state.cards[
                static_cast<std::size_t>(
                    DeckType::Chance
                )
            ];

        chance = {};
        chance.cardCount = ChanceCount;
        chance.jailOwner = NobodyPlayer;
        chance.jailOfferedInTradeTo =
            NobodyPlayer;

        shuffleCardDeck(
            chance,
            ChanceFirst,
            randomGenerator
        );
    }
}
