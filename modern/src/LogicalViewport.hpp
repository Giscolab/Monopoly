#pragma once

#include <optional>

namespace monopoly::logicalviewport
{
    inline constexpr double LogicalWidth = 800.0;
    inline constexpr double LogicalHeight = 600.0;

    struct Transform
    {
        double scale = 0.0;
        double offsetX = 0.0;
        double offsetY = 0.0;
        double pixelWidth = 0.0;
        double pixelHeight = 0.0;

        [[nodiscard]] bool valid() const noexcept;
    };

    struct LogicalPoint
    {
        double x = 0.0;
        double y = 0.0;
    };

    struct LogicalRect
    {
        double x = 0.0;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;
    };

    struct PixelRect
    {
        double x = 0.0;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;
    };

    // Fits the original 800x600 coordinate system inside a pixel extent while
    // preserving its aspect ratio. Any unused area forms centered letterbox
    // or pillarbox bars. A non-positive extent produces an invalid transform.
    [[nodiscard]] Transform makeTransform(
        int targetWidth,
        int targetHeight
    ) noexcept;

    // The content area is half-open: its top and left edges are included,
    // while its right and bottom edges belong to the surrounding bars/outside
    // area. This matches the coordinate range produced by pixel-based input.
    [[nodiscard]] std::optional<LogicalPoint> windowToLogical(
        const Transform& transform,
        double windowX,
        double windowY
    ) noexcept;

    // Converts a logical rectangle without clipping or integer rounding. The
    // caller can apply the rounding policy required by its rendering backend.
    [[nodiscard]] PixelRect logicalToPixelRect(
        const Transform& transform,
        const LogicalRect& logicalRect
    ) noexcept;
}
