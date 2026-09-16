#pragma once

#include "Display.hpp"
#include "RuleTypes.hpp"
#include "StatsUI.hpp"
#include "UIMessages.hpp"

#include <cstdint>
#include <vector>

namespace monopoly::statsui
{
    enum class FutureImmunityKind : std::uint8_t
    {
        Future = 0,
        Immunity
    };

    struct FutureImmunityIcon
    {
        FutureImmunityKind kind{FutureImmunityKind::Future};
        rules::PlayerNumber player{rules::NobodyPlayer};
        Rect rect{};
        friend bool operator==(
            const FutureImmunityIcon&, const FutureImmunityIcon&) = default;
    };

    struct FutureImmunityRow
    {
        int square{-1};
        std::int32_t hitCount{};
        rules::PlayerNumber fromPlayer{rules::NobodyPlayer};
    };
    struct FutureImmunityState
    {
        std::vector<FutureImmunityIcon> icons;
        std::vector<FutureImmunityRow> rows;
        FutureImmunityKind kind{FutureImmunityKind::Future};
        rules::PlayerNumber player{rules::NobodyPlayer};
        int scrollIndex{};
        bool upEnabled{};
        bool downEnabled{};
        bool open{};
    };

    inline constexpr Rect FutureImmunityPopupRect{601, 3, 798, 226};
    inline constexpr Rect FutureImmunityDownRect{770, 188, 798, 226};
    inline constexpr Rect FutureImmunityUpRect{770, 63, 798, 99};

    void resetFutureImmunity(FutureImmunityState& state) noexcept;
    void setFutureImmunityIcons(
        FutureImmunityState& state,
        std::vector<FutureImmunityIcon> icons);
    void refreshFutureImmunity(
        FutureImmunityState& state,
        const rules::GameState& gameState) noexcept;
    void syncFutureImmunityView(
        FutureImmunityState& state, Screen statsScreen,
        display::Screen2D view) noexcept;

    [[nodiscard]] bool processFutureImmunityInput(
        FutureImmunityState& state,
        const rules::GameState& gameState,
        Screen statsScreen,
        display::Screen2D view,
        const uimsg::Message& message) noexcept;
}
