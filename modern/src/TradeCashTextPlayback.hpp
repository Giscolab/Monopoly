#pragma once

#include "DataBanks.hpp"
#include "Display.hpp"
#include "SequencePlayback.hpp"
#include "TradeUI.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace monopoly::fonts { class Runtime; }

namespace monopoly::tradeui
{
    inline constexpr data::DataTag TradeCashIconTag = 0x0355;
    inline constexpr std::uint16_t TradeCashTextPriority = 474;
    inline constexpr std::array<std::uint16_t, 4> TradeCashIconPriorities{{
        100, 101, 102, 103}};
    inline constexpr std::array<std::int32_t, 4> TradeCashTextX{{
        10, 610, 210, 410}};
    inline constexpr std::array<std::int32_t, 4> TradeCashTextY{{
        428, 428, 391, 391}};
    inline constexpr std::array<std::int32_t, 4> TradeCashIconX{{
        9, 609, 209, 409}};
    inline constexpr std::array<std::int32_t, 4> TradeCashIconY{{
        395, 395, 358, 358}};

    class CashTextPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            const rules::GameState& gameState,
            display::Screen2D desiredView,
            int monetarySystem,
            fonts::Runtime* fontRuntime,
            engine::SequencePlayback& playback);

        void reset() noexcept;

    private:
        struct Published
        {
            data::DataId id{data::EmptyDataId};
            std::uint16_t priority{};
            std::int32_t x{};
            std::int32_t y{};
            bool operator==(const Published&) const = default;
        };

        [[nodiscard]] std::expected<void, std::string> ensureSurfaces(
            engine::SequencePlayback& playback);

        std::array<std::optional<data::DataId>, 4> textSurfaces_{};
        std::array<std::optional<std::string>, 4> textCache_{};
        std::vector<Published> current_;
    };
}
