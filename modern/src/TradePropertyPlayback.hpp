#pragma once

#include "DataBanks.hpp"
#include "Display.hpp"
#include "RuleTypes.hpp"
#include "SequencePlayback.hpp"
#include "TradeUI.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::tradeui
{
    inline constexpr data::DataTag TradePropertyMortgagedBaseTag = 0x05CA;
    inline constexpr data::DataTag TradePropertyNormalBaseTag = 0x05E6;
    inline constexpr std::array<std::uint16_t, 4> TradePropertyBasePriorities{{
        324, 374, 424, 474
    }};
    inline constexpr std::size_t TradePropertyPlaybackObjectCount =
        4 * static_cast<std::size_t>(rules::SquareCount);

    [[nodiscard]] data::DataId tradePropertyDataId(
        int square,
        bool mortgaged) noexcept;

    class PropertyPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            const rules::GameState& gameState,
            display::Screen2D desiredView,
            engine::SequencePlayback& playback);

        void reset() noexcept;

        struct ObjectState
        {
            data::DataId id{data::EmptyDataId};
            std::uint16_t priority{};
            std::int32_t x{};
            std::int32_t y{};

            bool operator==(const ObjectState&) const = default;
        };

    private:
        std::array<ObjectState, TradePropertyPlaybackObjectCount> current_{};
    };
}
