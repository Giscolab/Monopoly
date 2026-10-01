#pragma once

#include "DataBanks.hpp"
#include "Display.hpp"
#include "SequencePlayback.hpp"
#include "TradeUI.hpp"
#include "FontRuntime.hpp"

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

    inline constexpr std::uint16_t TradeCashReadoutPriority = 1978;
    inline constexpr std::uint32_t TradeCashReadoutWidth = 172;
    inline constexpr std::uint32_t TradeCashReadoutHeight = 14;

    class CashTextPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            const rules::GameState& gameState,
            display::Screen2D desiredView,
            int monetarySystem,
            fonts::Runtime* fontRuntime,
            engine::SequencePlayback& playback, bool modernAA = false,
            bool modernCashReadout = false);

        void reset() noexcept;
        [[nodiscard]] std::optional<data::DataId> readoutSurface() const noexcept
        { return readoutSurface_; }

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
        std::optional<data::DataId> readoutSurface_;
        std::optional<std::string> readoutText_;
        std::optional<fonts::Settings> readoutFont_;
        std::array<bool, 4> modernAA_{};
        std::array<std::optional<fonts::Settings>, 4> fontSettings_{};
    };
}
