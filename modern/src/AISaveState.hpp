#pragma once

#include "AIProfile.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace monopoly::ai::save
{
    inline constexpr std::size_t TradeTimerCount = 20;

    struct State
    {
        profile::Profile profile{};
        std::array<std::int64_t, TradeTimerCount> timeLastTrade{};
    };

    [[nodiscard]] bool encode(
        const State& state,
        std::vector<std::uint8_t>& result) noexcept;

    [[nodiscard]] bool decode(
        std::span<const std::uint8_t> data,
        State& result) noexcept;
}
