#pragma once

#include "SequencePlayback.hpp"
#include "Display.hpp"

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
    inline constexpr data::DataTag CalculatorNumberIdleBaseTag = 0x007D;
    inline constexpr data::DataTag CalculatorTextBoxTag = 0x0091;
    inline constexpr data::DataTag CalculatorEnterIdleTag = 0x01FE;
    inline constexpr data::DataTag CalculatorEnterIdleAlternateTag = 0x0388;

    class CalculatorPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            display::Screen2D desiredView, engine::SequencePlayback& playback);
        void reset() noexcept;
        [[nodiscard]] bool visible() const noexcept { return visible_; }

    private:        struct Published
        {
            data::DataId id{data::EmptyDataId};
            std::uint16_t priority{};
            friend bool operator==(const Published&, const Published&) = default;
        };

        static constexpr std::size_t SlotCount = 21;
        std::array<std::optional<Published>, SlotCount> published_{};
        bool visible_{};
    };
}
