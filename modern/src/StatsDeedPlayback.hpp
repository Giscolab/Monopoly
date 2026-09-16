#pragma once

#include "StatsPlayerPlayback.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace monopoly::statsui
{
    inline constexpr std::uint16_t DeedGridPriority = 510;
    inline constexpr int DeedGridX = 22;
    inline constexpr int DeedGridY = 234;
    inline constexpr int DeedGridColumnStep = 110;
    inline constexpr int DeedGridRowStep = 54;
    inline constexpr int DeedGridColumns = 7;

    class DeedPlayback final
    {
    public:
        struct Published
        {
            data::DataId id{data::EmptyDataId};
            std::uint16_t priority{};
            int x{};
            int y{};
            friend bool operator==(const Published&, const Published&) = default;
        };
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state, const rules::GameState& gameState,
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
