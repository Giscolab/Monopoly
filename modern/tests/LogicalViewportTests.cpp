#include "LogicalViewport.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
    int failures = 0;

    [[nodiscard]] bool nearlyEqual(
        double left,
        double right) noexcept
    {
        constexpr double Epsilon = 0.000'001;
        return std::abs(left - right) <= Epsilon;
    }

    void expect(
        bool condition,
        std::string_view description)
    {
        if (condition)
        {
            std::cout << "[PASS] " << description << '\n';
            return;
        }

        ++failures;
        std::cerr << "[FAIL] " << description << '\n';
    }

    void testNativeExtent()
    {
        using namespace monopoly::logicalviewport;

        const Transform transform =
            makeTransform(800, 600);

        expect(transform.valid(), "800x600 transform is valid");
        expect(nearlyEqual(transform.scale, 1.0), "800x600 scale is 1");
        expect(nearlyEqual(transform.offsetX, 0.0), "800x600 has no horizontal bar");
        expect(nearlyEqual(transform.offsetY, 0.0), "800x600 has no vertical bar");
        expect(nearlyEqual(transform.pixelWidth, 800.0), "800x600 content width is 800");
        expect(nearlyEqual(transform.pixelHeight, 600.0), "800x600 content height is 600");

        const auto origin =
            windowToLogical(transform, 0.0, 0.0);

        expect(
            origin.has_value() &&
            nearlyEqual(origin->x, 0.0) &&
            nearlyEqual(origin->y, 0.0),
            "top-left content edge maps to logical origin"
        );

        const auto lastInteriorPoint =
            windowToLogical(transform, 799.999, 599.999);

        expect(
            lastInteriorPoint.has_value() &&
            lastInteriorPoint->x < LogicalWidth &&
            lastInteriorPoint->y < LogicalHeight,
            "point immediately inside bottom-right edge is accepted"
        );

        expect(
            !windowToLogical(transform, 800.0, 599.0).has_value(),
            "right content edge is excluded"
        );

        expect(
            !windowToLogical(transform, 799.0, 600.0).has_value(),
            "bottom content edge is excluded"
        );
    }

    void testPillarboxExtent()
    {
        using namespace monopoly::logicalviewport;

        const Transform transform =
            makeTransform(1600, 900);

        expect(nearlyEqual(transform.scale, 1.5), "1600x900 scale is 1.5");
        expect(nearlyEqual(transform.offsetX, 200.0), "1600x900 horizontal offset is 200");
        expect(nearlyEqual(transform.offsetY, 0.0), "1600x900 vertical offset is zero");
        expect(nearlyEqual(transform.pixelWidth, 1200.0), "1600x900 content width is 1200");
        expect(nearlyEqual(transform.pixelHeight, 900.0), "1600x900 content height is 900");

        expect(
            !windowToLogical(transform, 199.999, 450.0).has_value(),
            "left pillarbox is rejected"
        );

        expect(
            !windowToLogical(transform, 1400.0, 450.0).has_value(),
            "right pillarbox begins at x=1400"
        );

        const auto center =
            windowToLogical(transform, 800.0, 450.0);

        expect(
            center.has_value() &&
            nearlyEqual(center->x, 400.0) &&
            nearlyEqual(center->y, 300.0),
            "1600x900 center maps to logical center"
        );

        const PixelRect fullRect =
            logicalToPixelRect(
                transform,
                { 0.0, 0.0, 800.0, 600.0 }
            );

        expect(
            nearlyEqual(fullRect.x, 200.0) &&
            nearlyEqual(fullRect.y, 0.0) &&
            nearlyEqual(fullRect.width, 1200.0) &&
            nearlyEqual(fullRect.height, 900.0),
            "full logical rectangle maps to centered pixel viewport"
        );

        const PixelRect tradeRect =
            logicalToPixelRect(
                transform,
                { 200.0, 0.0, 400.0, 225.0 }
            );

        expect(
            nearlyEqual(tradeRect.x, 500.0) &&
            nearlyEqual(tradeRect.y, 0.0) &&
            nearlyEqual(tradeRect.width, 600.0) &&
            nearlyEqual(tradeRect.height, 337.5),
            "logical sub-rectangle preserves offset and fractional scale"
        );
    }

    void testLetterboxExtent()
    {
        using namespace monopoly::logicalviewport;

        const Transform transform =
            makeTransform(1000, 1000);

        expect(nearlyEqual(transform.scale, 1.25), "1000x1000 scale is 1.25");
        expect(nearlyEqual(transform.offsetX, 0.0), "1000x1000 horizontal offset is zero");
        expect(nearlyEqual(transform.offsetY, 125.0), "1000x1000 vertical offset is 125");
        expect(nearlyEqual(transform.pixelWidth, 1000.0), "1000x1000 content width is 1000");
        expect(nearlyEqual(transform.pixelHeight, 750.0), "1000x1000 content height is 750");

        expect(
            !windowToLogical(transform, 500.0, 124.999).has_value(),
            "top letterbox is rejected"
        );

        expect(
            !windowToLogical(transform, 500.0, 875.0).has_value(),
            "bottom letterbox begins at y=875"
        );

        const auto topLeft =
            windowToLogical(transform, 0.0, 125.0);

        expect(
            topLeft.has_value() &&
            nearlyEqual(topLeft->x, 0.0) &&
            nearlyEqual(topLeft->y, 0.0),
            "letterboxed top-left content edge is accepted"
        );

        const auto center =
            windowToLogical(transform, 500.0, 500.0);

        expect(
            center.has_value() &&
            nearlyEqual(center->x, 400.0) &&
            nearlyEqual(center->y, 300.0),
            "1000x1000 center maps to logical center"
        );
    }

    void testInvalidInputs()
    {
        using namespace monopoly::logicalviewport;

        const Transform invalidWidth =
            makeTransform(0, 600);

        const Transform invalidHeight =
            makeTransform(800, -1);

        expect(!invalidWidth.valid(), "zero-width extent is invalid");
        expect(!invalidHeight.valid(), "negative-height extent is invalid");

        expect(
            !windowToLogical(invalidWidth, 0.0, 0.0).has_value(),
            "invalid transform rejects input"
        );

        const Transform native =
            makeTransform(800, 600);

        expect(
            !windowToLogical(
                native,
                std::numeric_limits<double>::quiet_NaN(),
                0.0
            ).has_value(),
            "non-finite input is rejected"
        );

        const PixelRect invalidRect =
            logicalToPixelRect(
                invalidWidth,
                { 10.0, 20.0, 30.0, 40.0 }
            );

        expect(
            nearlyEqual(invalidRect.x, 0.0) &&
            nearlyEqual(invalidRect.y, 0.0) &&
            nearlyEqual(invalidRect.width, 0.0) &&
            nearlyEqual(invalidRect.height, 0.0),
            "invalid transform produces an empty pixel rectangle"
        );
    }
}

int main()
{
    std::cout
        << "Monopoly logical viewport tests\n"
        << "===============================\n";

    testNativeExtent();
    testPillarboxExtent();
    testLetterboxExtent();
    testInvalidInputs();

    if (failures != 0)
    {
        std::cerr
            << failures
            << " logical viewport test(s) failed.\n";

        return 1;
    }

    std::cout
        << "All logical viewport tests passed.\n";

    return 0;
}
