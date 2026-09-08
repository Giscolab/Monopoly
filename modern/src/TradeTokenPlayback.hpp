#pragma once

#include "DataBanks.hpp"
#include "Display.hpp"
#include "RuleTypes.hpp"
#include "SequencePlayback.hpp"
#include "TradeUI.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::tradeui
{
    inline constexpr data::DataTag TradeTokenBaseTag = 0x01C0;
    inline constexpr std::uint16_t TradeTokenAPriority = 274;
    inline constexpr std::uint16_t TradeTokenBPriority = 275;
    inline constexpr std::int32_t TradeTokenAX = 10;
    inline constexpr std::int32_t TradeTokenRightEdge = 790;
    inline constexpr std::int32_t TradeTokenY = 235;

    [[nodiscard]] constexpr data::DataId tradeTokenSequence(
        std::uint8_t token) noexcept
    {
        return data::packDataId(data::LegacyGroupId::Main,
            static_cast<data::DataTag>(TradeTokenBaseTag + token));
    }

    class TokenPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            const rules::GameState& gameState,
            display::Screen2D desiredView,
            engine::SequencePlayback& playback);

        void reset() noexcept
        {
            current_.fill(data::EmptyDataId);
        }

    private:
        std::array<data::DataId, 2> current_{};
    };
}
