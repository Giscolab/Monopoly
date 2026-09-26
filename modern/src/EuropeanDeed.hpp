#pragma once

#include "BitmapRuntime.hpp"
#include "FontRuntime.hpp"

#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace monopoly::deeds
{
    inline constexpr int Width = 199;
    inline constexpr int Height = 227;

    struct Request
    {
        int square{};
        int languageId{2}; // Source language IDs: UK=2 .. Norwegian=10.
        int board{}; // Europe board 0..11; -1 uses installed language's board.
        int monetarySystem{}; // Source currency IDs 0..13.
        bool front{true};
        int housesPerHotel{5};
    };

    struct Fill
    {
        int x{}, y{}, width{}, height{};
        std::uint32_t color{};
    };

    struct TextRegion
    {
        std::string text;
        int y{}, height{}, justification{}, verticalLeeway{}, fontSize{};
        std::uint32_t color{};
        bool bold{}, italic{}, verticalCenter{};
    };

    struct Plan
    {
        data::DataId background{};
        std::vector<Fill> fills;
        std::vector<TextRegion> text;
    };

    // Unlike IBar's board-order property index, this preserves TRANS_PROP's
    // streets-first, railroads-then-utilities ordering.
    [[nodiscard]] int propertyIndex(int square) noexcept;
    [[nodiscard]] data::DataId templateId(const Request& request) noexcept;

    // Printed deed rents are canonical: source short-game table swaps are
    // compensated by CreateDeed. Planning does not depend on mutable rule state.
    [[nodiscard]] std::expected<Plan, std::string> plan(const Request& request);

    using TemplateResolver = std::function<std::expected<
        std::shared_ptr<const data::BitmapRuntimeAsset>, std::string>(data::DataId)>;

    // Uses the caller's retail Arial font and preserves all its settings.
    // Missing templates/font/render failures are errors, never blank deeds.
    [[nodiscard]] std::expected<data::LegacyBitmapRGBA8, std::string> render(
        const Request& request, fonts::Runtime& fonts,
        const TemplateResolver& resolveTemplate);
}
