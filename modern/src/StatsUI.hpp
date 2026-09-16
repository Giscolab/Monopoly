#pragma once

#include "Display.hpp"
#include "RuleTypes.hpp"
#include "UIMessages.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace monopoly::statsui
{
    enum class Screen : std::uint8_t
    {
        Player = 0,
        Deed = 1,
        Bank = 2
    };

    enum class BankDeedState : std::uint8_t
    {
        Hidden = 0,
        Available,
        Sold,
        Mortgaged
    };

    struct Rect
    {
        int left{};
        int top{};
        int right{};
        int bottom{};

        [[nodiscard]] constexpr bool contains(int x, int y) const noexcept
        {
            return x >= left && x < right && y >= top && y < bottom;
        }

        friend bool operator==(const Rect&, const Rect&) = default;
    };

    struct State
    {
        Screen screen{Screen::Player};
        std::array<std::uint8_t, 3> lastSort{};
        std::uint8_t activeSort{};
        std::array<rules::PlayerNumber, rules::MaxPlayers> playerOrder{};
        std::array<std::int64_t, rules::MaxPlayers> playerMetric{};
        std::size_t playerCount{};
        std::array<std::uint8_t, rules::SquareCount> deedOrder{};
        std::array<std::int64_t, rules::SquareCount> deedMetric{};
        std::array<int, rules::MaxPlayers> bankPlayerHouses{};
        std::array<int, rules::MaxPlayers> bankPlayerHotels{};
        int bankHousesRemaining{};
        int bankHotelsRemaining{};
        std::array<BankDeedState, rules::SquareCount> bankDeeds{};
        bool activeDatasetAvailable{true};
        bool portfolioVisible{};
        bool initialized{};
    };

    [[nodiscard]] constexpr Rect categoryRect(std::uint8_t index) noexcept
    {
        const int left = 54 + 93 * static_cast<int>(index);
        return {left, 510, left + 77 - 3 * static_cast<int>(index), 550};
    }

    [[nodiscard]] constexpr Rect sortRect(std::uint8_t index) noexcept
    {
        const int left = 495 + 70 * static_cast<int>(index);
        return {left, 510, left + 56 + 2 * static_cast<int>(index), 550};
    }

    void reset(State& state) noexcept;
    void refresh(State& state, const rules::GameState& gameState) noexcept;
    void syncView(
        State& state, const rules::GameState& gameState,
        display::Screen2D view) noexcept;
    [[nodiscard]] bool selectCategory(
        State& state, Screen screen, const rules::GameState& gameState) noexcept;
    [[nodiscard]] bool selectSort(
        State& state, std::uint8_t sortIndex,
        const rules::GameState& gameState) noexcept;
    [[nodiscard]] std::optional<Screen> categoryHit(int x, int y) noexcept;
    [[nodiscard]] std::optional<std::uint8_t> sortHit(int x, int y) noexcept;
    [[nodiscard]] bool processInput(
        State& state, const rules::GameState& gameState,
        display::Screen2D view, const uimsg::Message& message) noexcept;
}
