#pragma once

#include "Display.hpp"
#include "IBar.hpp"
#include "RuleTypes.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace monopoly::vintage
{
    // Ephemeral presentation snapshot: game state remains the only authority.
    struct PlayerView
    {
        rules::PlayerNumber index{};
        std::wstring name;
        std::int64_t cash{};
        std::uint8_t token{}, colour{};
        bool current{}, visible{};
    };
    struct PropertyView
    {
        int square{};
        rules::PlayerNumber owner{};
        std::uint8_t houses{};
        bool mortgaged{}, ownedByCurrentPlayer{};
    };
    struct Model
    {
        std::vector<PlayerView> players;
        std::vector<PropertyView> visibleProperties;
        ibar::layout::ActionButtonLayout actionLayout{};
        ibar::layout::ActionButtonMask activeActions{};
        display::Screen2D view{};
        bool hideCash{};
    };
    [[nodiscard]] Model makeModel(const rules::GameState& game,
        const ibar::State& bar, display::Screen2D view);
}
