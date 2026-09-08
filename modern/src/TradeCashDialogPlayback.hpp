#pragma once

#include "DataBanks.hpp"
#include "Display.hpp"
#include "SequencePlayback.hpp"
#include "TradeUI.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::tradeui
{
    inline constexpr data::DataTag TradeCashDialogTag = 0x118A;
    inline constexpr std::array<data::DataTag, 3> TradeCashIdleTags{{
        0x02D9, 0x02DB, 0x02DD}};
    inline constexpr std::array<data::DataTag, 3> TradeCashPressedTags{{
        0x02DA, 0x02DC, 0x02DE}};

    inline constexpr std::uint16_t TradeCashDialogPriority = 1975;
    inline constexpr std::uint16_t TradeCashButtonPriority = 1976;
    inline constexpr std::uint16_t TradeCashPressedPriority = 1977;

    inline constexpr std::array<std::int32_t, 2> TradeCashDialogX{{4, 604}};
    inline constexpr std::array<std::int32_t, 2> TradeCashDialogY{{324, 324}};
    inline constexpr std::array<std::int32_t, 2> TradeCashButtonX{{-306, 294}};
    inline constexpr std::array<std::int32_t, 2> TradeCashButtonY{{-29, -29}};

    [[nodiscard]] constexpr data::DataId tradeCashDialogSequence() noexcept
    {
        return data::packDataId(
            data::LegacyGroupId::LanguageGraphics, TradeCashDialogTag);
    }

    [[nodiscard]] constexpr data::DataId tradeCashIdleSequence(
        std::size_t index) noexcept
    {
        return index < TradeCashIdleTags.size()
            ? data::packDataId(
                data::LegacyGroupId::LanguageGraphics,
                TradeCashIdleTags[index])
            : data::EmptyDataId;
    }

    [[nodiscard]] constexpr data::DataId tradeCashPressedSequence(
        CashDialogFeedback feedback) noexcept
    {
        if (feedback == CashDialogFeedback::None) return data::EmptyDataId;
        const auto index = static_cast<std::size_t>(feedback) - 1u;
        return index < TradeCashPressedTags.size()
            ? data::packDataId(
                data::LegacyGroupId::LanguageGraphics,
                TradeCashPressedTags[index])
            : data::EmptyDataId;
    }

    class CashDialogPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            State& state,
            display::Screen2D desiredView,
            std::uint64_t nowTick,
            engine::SequencePlayback& playback);

        void reset() noexcept
        {
            visible_ = false;
            side_ = 0;
            feedback_ = data::EmptyDataId;
            feedbackStartTick_ = 0;
        }

        [[nodiscard]] bool visible() const noexcept { return visible_; }
        [[nodiscard]] std::uint8_t side() const noexcept { return side_; }

    private:
        bool visible_{};
        std::uint8_t side_{};
        data::DataId feedback_{data::EmptyDataId};
        std::uint64_t feedbackStartTick_{};
    };
}
