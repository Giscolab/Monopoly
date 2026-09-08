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
    inline constexpr std::uint16_t TradeBackdropPriority = 80;
    inline constexpr std::uint16_t TradeColourRailPriority = 95;
    inline constexpr std::uint16_t TradeDisplayBoardPriority = 1000;
    inline constexpr std::uint16_t TradeTurntablePriority = 130;
    inline constexpr std::uint16_t TradeArrowPriority = 140;
    inline constexpr std::uint16_t TradeDisplayPanelPriority = 145;
    inline constexpr std::uint16_t TradeFutureButtonPriority = 150;
    inline constexpr std::uint16_t TradeCreateButtonPriority = 155;
    inline constexpr std::uint16_t TradeImmunityButtonPriority = 160;

    inline constexpr std::uint8_t TradeLoopToBeginning = 3;
    inline constexpr data::DataTag TradeBackgroundTag = 0x02CD;
    inline constexpr data::DataTag TradeDisplayBoardTag = 0x02BB;
    inline constexpr data::DataTag TradeTurntableTag = 0x02DF;
    inline constexpr data::DataTag TradeArrowTag = 0x02AC;
    inline constexpr data::DataTag TradeDisplayLeftTag = 0x02CF;
    inline constexpr data::DataTag TradeDisplayRightTag = 0x02D0;
    inline constexpr data::DataTag TradeLeftColourBaseTag = 0x02AD;
    inline constexpr data::DataTag TradeRightColourBaseTag = 0x02B4;
    inline constexpr data::DataTag TradeCreateButtonTag = 0x02CE;
    inline constexpr data::DataTag TradeFutureButtonTag = 0x02D1;
    inline constexpr data::DataTag TradeImmunityButtonTag = 0x02D8;

    [[nodiscard]] constexpr data::DataId tradeBackdropSequence(
        data::DataTag tag) noexcept
    {
        return data::packDataId(data::LegacyGroupId::LanguageGraphics, tag);
    }

    class BackdropPlayback final
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

        [[nodiscard]] std::size_t activeCount() const noexcept;
        [[nodiscard]] bool visible() const noexcept
        {
            return activeCount() != 0;
        }

    private:
        static constexpr std::size_t SlotCount = 11;
        std::array<data::DataId, SlotCount> current_{};
    };
}
