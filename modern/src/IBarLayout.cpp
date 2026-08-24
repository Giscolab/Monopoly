#include "IBarLayout.hpp"

namespace monopoly::ibar::layout
{
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
}
