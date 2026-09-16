#pragma once

#include "Display.hpp"
#include "SequencePlayback.hpp"
#include "StatsCalculatorUI.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace monopoly::statsui
{
    inline constexpr std::uint16_t CalculatorBackgroundPriority = 50;
    inline constexpr std::uint16_t CalculatorButtonPriority = 100;
    inline constexpr data::DataTag CalculatorBackgroundTag = 0x006A;
    inline constexpr data::DataTag CalculatorFunctionIdleBaseTag = 0x006D;
    inline constexpr data::DataTag CalculatorFunctionPressBaseTag = 0x0075;
    inline constexpr data::DataTag CalculatorNumberIdleBaseTag = 0x007D;
    inline constexpr data::DataTag CalculatorNumberPressBaseTag = 0x0087;
    inline constexpr data::DataTag CalculatorTextBoxTag = 0x0091;
    inline constexpr data::DataTag CalculatorEnterIdleTag = 0x01FE;
    inline constexpr data::DataTag CalculatorEnterIdleAlternateTag = 0x0388;
    inline constexpr data::DataTag CalculatorTokenBaseTag = 0x01C0;
    inline constexpr float CalculatorTokenScale = 0.8F;

    class CalculatorPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            display::Screen2D desiredView, const CalculatorUIState& ui,
            const rules::GameState& gameState,
            engine::SequencePlayback& playback);
        void reset() noexcept;
        [[nodiscard]] bool visible() const noexcept { return visible_; }

    private:
        struct Published
        {
            data::DataId id{data::EmptyDataId};
            std::uint16_t priority{};
            friend bool operator==(const Published&, const Published&) = default;
        };

        static constexpr std::size_t SlotCount = 21 + rules::MaxPlayers;
        std::array<std::optional<Published>, SlotCount> published_{};
        bool visible_{};
    };
}
