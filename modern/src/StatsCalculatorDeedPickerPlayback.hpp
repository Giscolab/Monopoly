#pragma once

#include "StatsCalculatorUI.hpp"
#include "StatsPlayerPlayback.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace monopoly::statsui
{
    inline constexpr std::uint16_t CalculatorDeedPickerPriority = 610;
    inline constexpr int CalculatorDeedPickerBackgroundX = 200;
    inline constexpr int CalculatorDeedPickerBackgroundY = 230;
    inline constexpr int CalculatorDeedPickerBackgroundWidth = 400;
    inline constexpr int CalculatorDeedPickerBackgroundHeight = 200;
    inline constexpr int CalculatorDeedPickerX = 210;
    inline constexpr int CalculatorDeedPickerY = 240;
    inline constexpr int CalculatorDeedPickerColumnStep = 57;
    inline constexpr int CalculatorDeedPickerRowStep = 45;
    inline constexpr int CalculatorDeedPickerColumns = 7;

    class CalculatorDeedPickerPlayback final
    {
    public:
        struct Published
        {
            data::DataId id{data::EmptyDataId};
            std::uint16_t priority{};
            int x{};
            int y{};
            friend bool operator==(const Published&, const Published&) = default;
        };

        [[nodiscard]] std::expected<void, std::string> sync(
            const CalculatorUIState& ui, int city,
            display::Screen2D desiredView,
            engine::SequencePlayback& playback);

        void reset() noexcept
        {
            current_.clear();
            background_.reset();
            backgroundVisible_ = false;
        }
        [[nodiscard]] std::size_t objectCount() const noexcept
        {
            return current_.size() + (backgroundVisible_ ? 1U : 0U);
        }

    private:
        std::vector<Published> current_;
        std::optional<data::DataId> background_;
        bool backgroundVisible_{};
    };
}
