#pragma once

#include "DataBanks.hpp"
#include "Display.hpp"
#include "SequencePlayback.hpp"
#include "TradeUI.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::tradeui
{
    inline constexpr data::DataTag TradeCancelIdleTag = 0x01AF;
    inline constexpr data::DataTag TradeCancelOutTag = 0x01B1;
    inline constexpr data::DataTag TradeProposeIdleTag = 0x01B8;
    inline constexpr data::DataTag TradeProposeOutTag = 0x01BA;
    inline constexpr std::uint16_t TradeCancelPriority = 201;
    inline constexpr std::uint16_t TradeProposePriority = 202;
    inline constexpr std::uint8_t TradeActionEndingStop = 1;

    [[nodiscard]] constexpr data::DataId tradeActionSequence(
        data::DataTag tag) noexcept
    {
        return data::packDataId(data::LegacyGroupId::LanguageGraphics, tag);
    }

    class ActionButtonPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            display::Screen2D desiredView,
            engine::SequencePlayback& playback);

        void reset() noexcept { visible_ = false; }
        [[nodiscard]] bool visible() const noexcept { return visible_; }

    private:
        bool visible_{};
    };
}
