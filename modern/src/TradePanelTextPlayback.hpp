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
    inline constexpr std::uint16_t TradePanelTextPriority = 148;

    class PanelTextPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            display::Screen2D desiredView,
            fonts::Runtime* fontRuntime,
            engine::SequencePlayback& playback);

        void reset() noexcept;

    private:
        std::optional<data::DataId> surface_;
        std::optional<std::string> contentKey_;
        bool visible_{};
    };
}
