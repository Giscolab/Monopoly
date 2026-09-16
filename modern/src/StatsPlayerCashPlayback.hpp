#pragma once

#include "StatsPlayerPlayback.hpp"

#include <cstddef>
#include <expected>
#include <string>
#include <vector>

namespace monopoly::statsui
{
    inline constexpr data::DataTag PlayerCashIconTag = 0x0355;
    inline constexpr std::uint16_t PlayerCashPriority =
        PlayerBoxPriority + 1;
    inline constexpr int PlayerCashLocalX = 5;
    inline constexpr int PlayerCashLocalY = 30;

    class PlayerCashPlayback final
    {
    public:
        struct Published
        {
            int x{};
            int y{};
            friend bool operator==(const Published&, const Published&) = default;
        };
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            const rules::GameState& gameState,
            const PlayerPlaybackInputs& inputs,
            display::Screen2D desiredView,
            engine::SequencePlayback& playback);

        void reset() noexcept { current_.clear(); }
        [[nodiscard]] std::size_t objectCount() const noexcept
        {
            return current_.size();
        }

    private:
        std::vector<Published> current_;
    };
}
