#pragma once

#include "DataBanks.hpp"
#include "Display.hpp"
#include "RuleTypes.hpp"
#include "SequencePlayback.hpp"
#include "TradeUI.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace monopoly::fonts { class Runtime; }

namespace monopoly::tradeui
{
    inline constexpr std::uint16_t TradeNamePriority = 148;
    inline constexpr std::array<std::int32_t, 2> TradeNameX{{56, 604}};
    inline constexpr std::array<std::int32_t, 2> TradeNameY{{229, 229}};

    class NamePlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            const rules::GameState& gameState,
            display::Screen2D desiredView,
            fonts::Runtime* fontRuntime,
            engine::SequencePlayback& playback);

        void reset() noexcept;

    private:
        std::array<std::optional<data::DataId>, 2> surfaces_{};
        std::array<std::optional<std::string>, 2> textCache_{};
        std::array<bool, 2> visible_{};
    };
}
