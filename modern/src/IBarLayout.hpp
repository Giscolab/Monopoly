#pragma once

namespace monopoly::ibar::layout
{
    inline constexpr int VirtualWidth = 800;
    inline constexpr int VirtualHeight = 600;

    inline constexpr int ScoreY = 560;

    inline constexpr int ScoreBorder = 2;

    inline constexpr int BankWidth = 45;

    inline constexpr int ScoreBoxSmallWidth = 127;

    inline constexpr int ScoreBoxLargeWidth = 184;


    struct Rect
    {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;


        [[nodiscard]]
        bool contains(
            int x,
            int y
        ) const noexcept
        {
            // Win32 PtInRect :
            // right / bottom exclus.
            return
                x >= left &&
                x < right &&
                y >= top &&
                y < bottom;
        }
    };


    [[nodiscard]]
    int scoreBoxWidth(
        int numberOfPlayers
    ) noexcept;


    [[nodiscard]]
    int scoreX(
        int playerIndex,
        int numberOfPlayers,
        int playerWidth
    ) noexcept;


    [[nodiscard]]
    Rect playerSetupHitRect(
        int playerIndex,
        int numberOfPlayers
    ) noexcept;
}
