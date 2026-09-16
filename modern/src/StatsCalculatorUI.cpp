#include "StatsCalculatorUI.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace monopoly::statsui
{
    namespace
    {
        [[nodiscard]] bool calculatorButtonsVisible(
            const CalculatorUIState& state) noexcept
        {
            return state.picker != CalculatorPicker::Player;
        }

        [[nodiscard]] bool publishResult(
            CalculatorUIState& state,
            const rules::GameState& gameState) noexcept
        {
            if (!state.activeFunction) return false;
            auto result = calculate(
                *state.activeFunction, gameState, state.selection);
            if (!result)
            {
                state.result.reset();
                state.error = result.error();
                return false;
            }
            state.result = *result;
            state.error.reset();
            return true;
        }
        void beginFunction(
            CalculatorUIState& state, std::uint8_t index) noexcept
        {
            state.activeFunction = static_cast<CalculatorFunction>(index);
            state.hoveredFunction = index;
            state.selection = {};
            state.hoveredDeed.reset();
            state.result.reset();
            state.error.reset();
            state.pressedNumber.reset();
            state.step = CalculatorStep::Second;

            switch (*state.activeFunction)
            {
            case CalculatorFunction::Odds:
            case CalculatorFunction::FutureValueToYou:
            case CalculatorFunction::FutureValueToOther:
                state.picker = CalculatorPicker::Deed;
                break;
            case CalculatorFunction::NetWorth:
            case CalculatorFunction::MaximumExpense:
            case CalculatorFunction::CurrentIncome:
            case CalculatorFunction::MaximumIncome:
            case CalculatorFunction::PotentialIncome:
                state.picker = CalculatorPicker::Player;
                break;
            }
        }
        [[nodiscard]] bool chooseDeed(
            CalculatorUIState& state, const rules::GameState& gameState,
            int x, int y) noexcept
        {
            const auto deed = calculatorDeedHit(x, y);
            if (!deed) return false;
            state.selection.deed = *deed;
            state.hoveredDeed.reset();

            if (state.activeFunction == CalculatorFunction::FutureValueToOther)
            {
                state.picker = CalculatorPicker::Player;
                state.step = CalculatorStep::Third;
                return true;
            }

            state.picker = CalculatorPicker::None;
            state.step = CalculatorStep::Fourth;
            (void)publishResult(state, gameState);
            return true;
        }

        [[nodiscard]] bool choosePlayer(
            CalculatorUIState& state, const rules::GameState& gameState,
            int x, int y) noexcept
        {
            const auto player = calculatorTokenHit(gameState, x, y);
            if (!player) return false;
            state.selection.player = *player;
            state.picker = CalculatorPicker::None;
            if (state.activeFunction == CalculatorFunction::FutureValueToOther)
            {
                // Retail THIRD_STEP presses CalculatorButton[player].
                state.pressedNumber = static_cast<std::uint8_t>(*player);
                state.step = CalculatorStep::Fourth;
                (void)publishResult(state, gameState);
                return true;
            }

            if (state.activeFunction == CalculatorFunction::MaximumExpense ||
                state.activeFunction == CalculatorFunction::CurrentIncome ||
                state.activeFunction == CalculatorFunction::MaximumIncome ||
                state.activeFunction == CalculatorFunction::PotentialIncome)
            {
                // June 25 retail path hard-wires one turn and visually presses 1.
                state.selection.turns = 1;
                state.pressedNumber = 0;
                state.step = CalculatorStep::Fourth;
                (void)publishResult(state, gameState);
                return true;
            }

            state.step = CalculatorStep::Third;
            (void)publishResult(state, gameState);
            return true;
        }
    }

    std::optional<Rect> calculatorDeedRect(int square) noexcept
    {
        const int property = ibar::layout::propertyIndex(square);
        if (property < 0) return std::nullopt;
        const int left = 210 + 57 * (property % 7);
        const int top = 240 + 45 * (property / 7);
        return Rect{left, top, left + 36, top + 42};
    }
    std::optional<std::uint8_t> calculatorFunctionHit(
        int x, int y) noexcept
    {
        for (std::uint8_t index = 0; index < 8; ++index)
            if (calculatorFunctionRect(index).contains(x, y)) return index;
        return std::nullopt;
    }

    std::optional<rules::PlayerNumber> calculatorTokenHit(
        const rules::GameState& state, int x, int y) noexcept
    {
        const auto count = std::min<std::size_t>(
            state.numberOfPlayers, rules::MaxPlayers);
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto player = static_cast<rules::PlayerNumber>(index);
            if (calculatorTokenRect(player).contains(x, y)) return player;
        }
        return std::nullopt;
    }

    std::optional<int> calculatorDeedHit(int x, int y) noexcept
    {
        for (int square = 0; square < static_cast<int>(rules::SquareCount); ++square)
        {
            const auto rect = calculatorDeedRect(square);
            if (rect && rect->contains(x, y)) return square;
        }
        return std::nullopt;
    }
    void resetCalculatorUI(CalculatorUIState& state) noexcept
    {
        state = {};
    }

    void syncCalculatorView(
        CalculatorUIState& state, display::Screen2D view) noexcept
    {
        const bool visible = view == display::Screen2D::Portfolio;
        if (!visible && state.visible)
        {
            resetCalculatorUI(state);
            return;
        }
        state.visible = visible;
    }

    bool processCalculatorInput(
        CalculatorUIState& state, const rules::GameState& gameState,
        display::Screen2D view, const uimsg::Message& message) noexcept
    {
        syncCalculatorView(state, view);
        if (!state.visible) return false;

        const int x = static_cast<int>(message.numberA);
        const int y = static_cast<int>(message.numberB);
        if (message.type == uimsg::Type::MouseMoved)
        {
            if (state.picker == CalculatorPicker::Deed)
            {
                state.hoveredDeed = calculatorDeedHit(x, y);
                return false;
            }
            state.hoveredDeed.reset();
            if (state.step >= CalculatorStep::Second) return false;
            const auto function = calculatorFunctionHit(x, y);
            if (function)
            {
                state.hoveredFunction = *function;
                state.step = CalculatorStep::First;
            }
            else
            {
                state.hoveredFunction.reset();
                state.activeFunction.reset();
                state.result.reset();
                state.error.reset();
                state.picker = CalculatorPicker::None;
                state.pressedNumber.reset();
                state.step = CalculatorStep::RollOver;
            }
            return false;
        }

        if (message.type != uimsg::Type::MouseLeftDown) return false;

        if (calculatorButtonsVisible(state) &&
            calculatorNumberRect(10).contains(x, y))
        {
            resetCalculatorUI(state);
            state.visible = true;
            return true;
        }

        switch (state.step)
        {
        case CalculatorStep::RollOver:
            return false;

        case CalculatorStep::First:
        {
            const auto function = calculatorFunctionHit(x, y);
            if (!function) return false;
            beginFunction(state, *function);
            return true;
        }
        case CalculatorStep::Second:
            if (!state.activeFunction) return false;
            switch (*state.activeFunction)
            {
            case CalculatorFunction::Odds:
            case CalculatorFunction::FutureValueToYou:
            case CalculatorFunction::FutureValueToOther:
                return chooseDeed(state, gameState, x, y);
            case CalculatorFunction::NetWorth:
            case CalculatorFunction::MaximumExpense:
            case CalculatorFunction::CurrentIncome:
            case CalculatorFunction::MaximumIncome:
            case CalculatorFunction::PotentialIncome:
                return choosePlayer(state, gameState, x, y);
            }
            return false;

        case CalculatorStep::Third:
            if (state.activeFunction == CalculatorFunction::FutureValueToOther)
                return choosePlayer(state, gameState, x, y);
            return false;

        case CalculatorStep::Fourth:
            return false;
        }
        return false;
    }
}
