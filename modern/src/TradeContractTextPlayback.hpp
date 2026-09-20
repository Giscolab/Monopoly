#pragma once

#include "DataBanks.hpp"
#include "Display.hpp"
#include "SequencePlayback.hpp"
#include "TradeUI.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace monopoly::fonts { class Runtime; }

namespace monopoly::tradeui
{
    // UDTrade_ProcessEverything starts rightpanel at 148, independently of
    // FutureTradeDlg.priority (524), which is used to place the arrows at 525.
    inline constexpr std::uint16_t TradeContractTextPriority = 148;

    class ContractTextPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            const rules::GameState& gameState,
            display::Screen2D desiredView,
            int city,
            fonts::Runtime* fontRuntime,
            engine::SequencePlayback& playback);

        void reset() noexcept;

    private:
        std::optional<data::DataId> surface_;
        std::optional<std::string> contentKey_;
        bool visible_{};
    };
}
