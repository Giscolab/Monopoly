#include "LogicalViewport.hpp"

#include <algorithm>
#include <cmath>

namespace monopoly::logicalviewport
{
    bool Transform::valid() const noexcept
    {
        return
            std::isfinite(scale) &&
            std::isfinite(offsetX) &&
            std::isfinite(offsetY) &&
            std::isfinite(pixelWidth) &&
            std::isfinite(pixelHeight) &&
            scale > 0.0 &&
            pixelWidth > 0.0 &&
            pixelHeight > 0.0;
    }

    namespace
    {
        Transform fitCanvas(
            int targetWidth,
            int targetHeight,
            double logicalWidth,
            double logicalHeight) noexcept
        {
            if (targetWidth <= 0 || targetHeight <= 0 ||
                logicalWidth <= 0.0 || logicalHeight <= 0.0)
                return {};

            const double width = static_cast<double>(targetWidth);
            const double height = static_cast<double>(targetHeight);
            const double scale = std::min(
                width / logicalWidth,
                height / logicalHeight);
            const double pixelWidth = logicalWidth * scale;
            const double pixelHeight = logicalHeight * scale;

            return {
                scale,
                (width - pixelWidth) / 2.0,
                (height - pixelHeight) / 2.0,
                pixelWidth,
                pixelHeight
            };
        }
    }


    Transform makeTransform(
        int targetWidth,
        int targetHeight) noexcept
    {
        return fitCanvas(
            targetWidth,
            targetHeight,
            LogicalWidth,
            LogicalHeight);
    }


    Transform makeWorld3DTransform(
        int targetWidth,
        int targetHeight) noexcept
    {
        return fitCanvas(
            targetWidth,
            targetHeight,
            World3DWidth,
            World3DHeight);
    }

    std::optional<LogicalPoint> windowToLogical(
        const Transform& transform,
        double windowX,
        double windowY) noexcept
    {
        if (!transform.valid() ||
            !std::isfinite(windowX) ||
            !std::isfinite(windowY))
        {
            return std::nullopt;
        }

        const double right =
            transform.offsetX + transform.pixelWidth;

        const double bottom =
            transform.offsetY + transform.pixelHeight;

        if (windowX < transform.offsetX ||
            windowY < transform.offsetY ||
            windowX >= right ||
            windowY >= bottom)
        {
            return std::nullopt;
        }

        return LogicalPoint
        {
            (windowX - transform.offsetX) /
                transform.scale,

            (windowY - transform.offsetY) /
                transform.scale
        };
    }

    PixelRect logicalToPixelRect(
        const Transform& transform,
        const LogicalRect& logicalRect) noexcept
    {
        if (!transform.valid())
        {
            return {};
        }

        return
        {
            transform.offsetX +
                logicalRect.x * transform.scale,

            transform.offsetY +
                logicalRect.y * transform.scale,

            logicalRect.width * transform.scale,
            logicalRect.height * transform.scale
        };
    }
}
