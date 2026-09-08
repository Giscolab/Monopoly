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
    inline constexpr data::DataTag TradeChanceJailTag = 0x00DD;
    inline constexpr data::DataTag TradeCommunityJailTag = 0x00DE;
    inline constexpr data::DataTag TradeFutureTag = 0x1027;
    inline constexpr data::DataTag TradeImmunityTag = 0x1053;
    inline constexpr std::uint16_t TradeIconBasePriority = 274;
    inline constexpr std::size_t TradeIconSlots = 4;

    inline constexpr std::array<std::int32_t, TradeIconSlots> TradeChanceX{{66,666,266,466}};
    inline constexpr std::array<std::int32_t, TradeIconSlots> TradeChanceY{{395,395,358,358}};
    inline constexpr std::array<std::int32_t, TradeIconSlots> TradeCommunityX{{66,666,266,466}};
    inline constexpr std::array<std::int32_t, TradeIconSlots> TradeCommunityY{{420,420,383,383}};
    inline constexpr std::array<std::int32_t, TradeIconSlots> TradeFutureX{{104,704,304,504}};
    inline constexpr std::array<std::int32_t, TradeIconSlots> TradeFutureY{{398,398,361,361}};
    inline constexpr std::array<std::int32_t, TradeIconSlots> TradeImmunityX{{104,704,304,504}};
    inline constexpr std::array<std::int32_t, TradeIconSlots> TradeImmunityY{{422,422,385,385}};

    [[nodiscard]] constexpr data::DataId tradeJailIcon(std::size_t deck) noexcept
    {
        return deck < 2
            ? data::packDataId(data::LegacyGroupId::Main,
                static_cast<data::DataTag>(TradeChanceJailTag + deck))
            : data::EmptyDataId;
    }

    [[nodiscard]] constexpr data::DataId tradeContractIcon(std::size_t kind) noexcept
    {
        data::DataTag tag{};
        if (kind == 0) tag = TradeFutureTag;
        else if (kind == 1) tag = TradeImmunityTag;
        return tag != 0
            ? data::packDataId(data::LegacyGroupId::LanguageGraphics, tag)
            : data::EmptyDataId;
    }

    class OfferIconPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            display::Screen2D desiredView,
            engine::SequencePlayback& playback);

        void reset() noexcept { current_.fill(false); }

    private:
        static constexpr std::size_t ObjectCount = 16;
        std::array<bool, ObjectCount> current_{};
    };
}
