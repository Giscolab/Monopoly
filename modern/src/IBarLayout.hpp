#pragma once

#include <cstdint>
#include <optional>

namespace monopoly::ibar::layout
{
    inline constexpr int VirtualWidth = 800;
    inline constexpr int VirtualHeight = 600;

    inline constexpr int ScoreY = 560;

    inline constexpr int ScoreBorder = 2;

    inline constexpr int BankWidth = 45;

    inline constexpr int ScoreBoxSmallWidth = 127;

    inline constexpr int ScoreBoxLargeWidth = 184;


    enum class ActionButtonSlot : int
    {
        Options = 0,
        Trade,
        General1,
        General4,
        General2,
        General3,
        Main,
        Camera,
        Status,

        Count
    };


    enum class ActionButtonLayout : int
    {
        General = 0,
        BuyAuction,
        TaxDecision,
        Trading
    };


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

    [[nodiscard]] constexpr Rect bankHitRect() noexcept
    {
        return {VirtualWidth - BankWidth, ScoreY, VirtualWidth, ScoreY + 32};
    }


    [[nodiscard]]
    Rect actionButtonRect(
        ActionButtonSlot slot,
        ActionButtonLayout layout = ActionButtonLayout::General
    ) noexcept;


    using PropertyMask = std::uint32_t;

    [[nodiscard]] int propertyIndex(int square) noexcept;
    [[nodiscard]] PropertyMask propertyBit(int square) noexcept;
    [[nodiscard]] int propertyBarOrder(int square) noexcept;
    [[nodiscard]] Rect propertyRect(int square) noexcept;
    [[nodiscard]] std::optional<int> propertyHit(
        int x, int y, PropertyMask visibleProperties) noexcept;


    using ActionButtonMask = unsigned int;

    [[nodiscard]]
    constexpr ActionButtonMask actionButtonBit(ActionButtonSlot slot) noexcept
    {
        return 1u << static_cast<unsigned int>(slot);
    }

    inline constexpr ActionButtonMask AllActionButtonSlots =
        (1u << static_cast<unsigned int>(ActionButtonSlot::Count)) - 1u;


    [[nodiscard]]
    std::optional<ActionButtonSlot> actionButtonHit(
        int x,
        int y,
        ActionButtonLayout layout = ActionButtonLayout::General,
        ActionButtonMask activeSlots = AllActionButtonSlots
    ) noexcept;
}
