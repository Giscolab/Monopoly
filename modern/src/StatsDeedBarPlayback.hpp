#pragma once

#include "StatsDeedPlayback.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace monopoly::statsui
{
    inline constexpr data::DataTag DeedOwnerBarBaseTag = 0x00C7;
    inline constexpr std::uint16_t DeedOwnerBarPriority = 510;
    inline constexpr int DeedOwnerBarOffsetX = 42;
    inline constexpr int DeedOwnerBarOffsetY = 13;

    class DeedBarPlayback final
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
