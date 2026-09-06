#include "IBarLayout.hpp"

#include <array>

namespace monopoly::ibar::layout
{
    namespace
    {
        constexpr std::array<int, 42> PropertyConversion{{
            -1, 0,-1, 1,-1, 2, 3,-1, 4, 5,
            -1, 6, 7, 8, 9,10,11,-1,12,13,
            -1,14,-1,15,16,17,18,19,20,21,
            -1,22,23,-1,24,25,-1,26,-1,27,
            -1,-1
        }};

        constexpr std::array<int, 28> PropertyBarOrder{{
            11, 9, 2, 14,13,12, 17,8,16,15,
            0,20,19,18, 23,22,21,5, 26,25,6,24,
            29,28,27, 3,32,30
        }};
    }
    int propertyIndex(int square) noexcept
    {
        if (square < 0 || square >= static_cast<int>(PropertyConversion.size()))
            return -1;
        return PropertyConversion[static_cast<std::size_t>(square)];
    }


    PropertyMask propertyBit(int square) noexcept
    {
        const int index = propertyIndex(square);
        return index < 0 ? 0u : (1u << static_cast<unsigned int>(index));
    }


    int propertyBarOrder(int square) noexcept
    {
        const int index = propertyIndex(square);
        if (index < 0) return -1;
        return PropertyBarOrder[static_cast<std::size_t>(index)];
    }


    Rect propertyRect(int square) noexcept
    {
        const int order = propertyBarOrder(square);
        if (order < 0) return {};

        int x = 0;
        int y = 0;
        if (order < 18)
        {
            x = 28 + 54 * (order / 3) - 5 * (order % 3);
            y = 495 + 10 * (order % 3);
        }
        else
        {
            x = 482 + 54 * ((order - 18) / 3) + 5 * (order % 3);
            y = 495 + 10 * (order % 3);
        }
        return {x, y, x + 34, y + 42};
    }


    std::optional<int> propertyHit(
        int x, int y, PropertyMask visibleProperties) noexcept
    {
        for (int square = 41; square >= 0; --square)
        {
            const auto bit = propertyBit(square);
            if (bit == 0 || (visibleProperties & bit) == 0) continue;
            if (propertyRect(square).contains(x, y)) return square;
        }
        return std::nullopt;
    }


    int scoreBoxWidth(
        int numberOfPlayers) noexcept
    {
        return
            numberOfPlayers <= 4
                ? ScoreBoxLargeWidth
                : ScoreBoxSmallWidth;
    }


    int scoreX(
        int playerIndex,
        int numberOfPlayers,
        int playerWidth) noexcept
    {
        if (
            playerIndex < 0 ||
            numberOfPlayers <= 0 ||
            playerIndex >= numberOfPlayers)
        {
            return -1;
        }


        const int availableWidth =
            VirtualWidth -
            ScoreBorder * 2 -
            BankWidth;


        const int spacing =
            (
                availableWidth -
                numberOfPlayers *
                    playerWidth
            ) /
            (
                numberOfPlayers + 1
            );


        return
            ScoreBorder +
            spacing *
                (playerIndex + 1) +
            playerWidth *
                playerIndex;
    }


    Rect playerSetupHitRect(
        int playerIndex,
        int numberOfPlayers) noexcept
    {
        const int width =
            scoreBoxWidth(
                numberOfPlayers
            );


        const int x =
            scoreX(
                playerIndex,
                numberOfPlayers,
                width
            );


        if (x < 0)
        {
            return {};
        }


        // L'original prend la hauteur directement dans
        // PlayerColorBarShownID.
        //
        // Les DAT_MAIN graphiques correspondants ne sont pas
        // présents dans l'archive source actuelle.
        //
        // Pour le Player Select uniquement, la barre occupe la
        // région basse connue :
        //
        // DISPLAY_ScoreY = 560
        // framebuffer     = 600
        //
        // Lorsque TAB_inpsl0 / TAB_inpss0 seront décodés,
        // seule cette hauteur sera remplacée par celle du chunk.
        return
        {
            x,
            ScoreY,
            x + width,
            VirtualHeight
        };
    }

    Rect actionButtonRect(
        ActionButtonSlot slot,
        ActionButtonLayout layout) noexcept
    {
        static constexpr std::array<Rect, 9> GeneralRects{{
            { 10, 455,  69, 483},
            { 70, 455, 132, 483},
            {148, 455, 252, 483},
            {569, 455, 672, 483},
            {256, 455, 357, 483},
            {464, 455, 567, 483},
            {361, 455, 463, 483},
            {690, 456, 727, 483},
            {732, 456, 789, 483}
        }};

        const int index = static_cast<int>(slot);
        if (index < 0 || index >= static_cast<int>(GeneralRects.size()))
            return {};

        Rect result = GeneralRects[static_cast<std::size_t>(index)];
        switch (layout)
        {
        case ActionButtonLayout::BuyAuction:
            if (slot == ActionButtonSlot::Main)
            {
                result.left = 251;
                result.right = 354;
            }
            else if (slot == ActionButtonSlot::General3)
            {
                result.left = 467;
                result.right = 569;
            }
            break;
        case ActionButtonLayout::TaxDecision:
            if (slot == ActionButtonSlot::Main)
            {
                result.left = 254;
                result.right = 355;
            }
            else if (slot == ActionButtonSlot::General3)
            {
                result.left = 464;
                result.right = 567;
            }
            break;
        case ActionButtonLayout::Trading:
            if (slot == ActionButtonSlot::Main)
                result = {349, 420, 449, 448};
            else if (slot == ActionButtonSlot::General2)
                result = {224, 420, 324, 448};
            else if (slot == ActionButtonSlot::General3)
                result = {473, 420, 573, 448};
            break;
        case ActionButtonLayout::General:
        default:
            break;
        }
        return result;
    }


    std::optional<ActionButtonSlot> actionButtonHit(
        int x,
        int y,
        ActionButtonLayout layout,
        ActionButtonMask activeSlots) noexcept
    {
        for (int index = 0;
             index < static_cast<int>(ActionButtonSlot::Count);
             ++index)
        {
            const auto slot = static_cast<ActionButtonSlot>(index);
            if ((activeSlots & actionButtonBit(slot)) == 0)
                continue;
            if (actionButtonRect(slot, layout).contains(x, y))
                return slot;
        }
        return std::nullopt;
    }

}
