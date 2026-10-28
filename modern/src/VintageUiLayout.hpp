#pragma once

#include "IBarLayout.hpp"
#include "RuleTypes.hpp"
#include <array>
#include <cstddef>
#include <optional>

namespace monopoly::vintage
{
    struct Point { float x{}, y{}; };
    struct Rect
    {
        float x{}, y{}, w{}, h{};
        [[nodiscard]] bool contains(Point p) const noexcept
        { return w > 0 && h > 0 && p.x >= x && p.y >= y && p.x < x+w && p.y < y+h; }
    };
    enum class HitKind { None, Action, Player, Property };
    struct Hit { HitKind kind{HitKind::None}; std::size_t index{}; };

    // Independent composition shared by rendering and input. The array index
    // for actions is the existing IBar slot, never a new game command.
    struct Layout
    {
        static constexpr float canvasWidth = 1600, canvasHeight = 900;
        static constexpr std::size_t actionCount = static_cast<std::size_t>(ibar::layout::ActionButtonSlot::Count);
        std::array<Rect, actionCount> actionButtons{};
        std::array<Rect, rules::MaxPlayers> playerPanels{};
        std::array<Rect, 6> propertyCards{};
        Rect tokenStage{};
        Rect modal{};
        unsigned playerCount{};
        explicit Layout(unsigned players = 4) noexcept;
        [[nodiscard]] Hit hit(Point point, ibar::layout::ActionButtonMask active =
            ibar::layout::AllActionButtonSlots) const noexcept;
        [[nodiscard]] std::optional<ibar::layout::ActionButtonSlot> actionHit(
            Point point, ibar::layout::ActionButtonMask active) const noexcept;
        // Reject invalid dimensions, nonfinite points and letterbox gutters.
        [[nodiscard]] std::optional<Point> toCanvas(Point physical, int width, int height) const noexcept;
        // Returns {0,0} for invalid dimensions or nonfinite points.
        [[nodiscard]] Point toWindow(Point canvas, int width, int height) const noexcept;
    };

    // Normalized logical transforms; these do not change the legacy viewport.
    [[nodiscard]] Point canvasToLegacy(Point point) noexcept;
    [[nodiscard]] Point legacyToCanvas(Point point) noexcept;
}
