#pragma once

#include "Display.hpp"
#include "IBarLayout.hpp"
#include "StatsCalculatorLogic.hpp"
#include "StatsUI.hpp"
#include "UIMessages.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace monopoly::statsui
{
    enum class CalculatorStep : std::uint8_t
    {
        RollOver = 0,
        First,
        Second,
        Third,
        Fourth
    };

    enum class CalculatorPicker : std::uint8_t
    {
        None = 0,
        Deed,
        Player
    };
    struct CalculatorUIState
    {
        CalculatorStep step{CalculatorStep::RollOver};
        CalculatorPicker picker{CalculatorPicker::None};
        std::optional<std::uint8_t> hoveredFunction;
        std::optional<CalculatorFunction> activeFunction;
        CalculatorSelection selection{};
        std::optional<CalculatorResult> result;
        std::optional<std::string> error;
        std::optional<std::uint8_t> pressedNumber;
        bool visible{};
    };

    [[nodiscard]] constexpr Rect calculatorFunctionRect(
        std::uint8_t index) noexcept
    {
        const int left = 410 + 49 * static_cast<int>(index % 2);
        const int top = 47 + 34 * static_cast<int>(index / 2);
        return {left, top, left + 47, top + 29};
    }

    [[nodiscard]] constexpr Rect calculatorNumberRect(
        std::uint8_t index) noexcept
    {
        if (index == 10) return {540, 169, 590, 194};
        const int left = 514 + 26 * static_cast<int>(index % 3);
        const int top = 79 + 30 * static_cast<int>(index / 3);
        return {left, top, left + 24, top + 25};
    }
    [[nodiscard]] constexpr Rect calculatorTokenRect(
        rules::PlayerNumber player) noexcept
    {
        const int column = static_cast<int>(player % 2);
        const int row = static_cast<int>(player / 2);
        const int left = 514 + (column == 0 ? 0 : 46);
        const int top = 79 + 30 * row;
        return {left, top, left + 42, top + 20};
    }

    [[nodiscard]] std::optional<Rect> calculatorDeedRect(
        int square) noexcept;
    [[nodiscard]] std::optional<std::uint8_t> calculatorFunctionHit(
        int x, int y) noexcept;
    [[nodiscard]] std::optional<rules::PlayerNumber> calculatorTokenHit(
        const rules::GameState& state, int x, int y) noexcept;
    [[nodiscard]] std::optional<int> calculatorDeedHit(
        int x, int y) noexcept;

    void resetCalculatorUI(CalculatorUIState& state) noexcept;
    void syncCalculatorView(
        CalculatorUIState& state, display::Screen2D view) noexcept;
    [[nodiscard]] bool processCalculatorInput(
        CalculatorUIState& state, const rules::GameState& gameState,
        display::Screen2D view, const uimsg::Message& message) noexcept;
}
