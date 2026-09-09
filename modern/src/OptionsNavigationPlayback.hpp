#pragma once

#include "OptionsUI.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace monopoly::optionsui
{
    enum class NavigationVisual : std::uint8_t
    {
        Off = 0,
        Idle,
        Return,
        Press
    };

    inline constexpr std::uint8_t NavigationStayAtEnd = 2;

    inline constexpr std::array<data::DataTag, 4> NavigationIdleTags{
        0x018E, 0x0192, 0x0186, 0x018A};
    inline constexpr std::array<data::DataTag, 4> NavigationReturnTags{
        0x018F, 0x0193, 0x0187, 0x018B};
    inline constexpr std::array<data::DataTag, 4> NavigationPressTags{
        0x0190, 0x0194, 0x0188, 0x018C};

    inline constexpr std::array<std::uint16_t, 4> NavigationIdlePriorities{
        1006, 1008, 1004, 1001};
    inline constexpr std::array<std::uint16_t, 4> NavigationPressPriorities{
        1007, 1009, 1005, 1003};

    [[nodiscard]] constexpr data::DataId navigationSequence(
        MenuButton button, NavigationVisual visual) noexcept
    {
        const auto index = static_cast<std::size_t>(button);
        if (index >= NavigationIdleTags.size() || visual == NavigationVisual::Off)
            return data::EmptyDataId;
        const auto tag = visual == NavigationVisual::Idle ? NavigationIdleTags[index]
            : visual == NavigationVisual::Return ? NavigationReturnTags[index]
            : NavigationPressTags[index];
        return data::packDataId(data::LegacyGroupId::LanguageGraphics, tag);
    }
    [[nodiscard]] constexpr std::uint16_t navigationPriority(
        MenuButton button, NavigationVisual visual) noexcept
    {
        const auto index = static_cast<std::size_t>(button);
        if (index >= NavigationIdlePriorities.size())
            return 0;
        return visual == NavigationVisual::Press
            ? NavigationPressPriorities[index]
            : NavigationIdlePriorities[index];
    }

    class NavigationPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            display::Screen2D desiredView,
            engine::SequencePlayback& playback);

        void reset() noexcept
        {
            visual_.fill(NavigationVisual::Off);
            selected_.reset();
        }

        [[nodiscard]] NavigationVisual visual(MenuButton button) const noexcept
        {
            const auto index = static_cast<std::size_t>(button);
            return index < visual_.size() ? visual_[index] : NavigationVisual::Off;
        }
    private:
        std::array<NavigationVisual, 4> visual_{
            NavigationVisual::Off,
            NavigationVisual::Off,
            NavigationVisual::Off,
            NavigationVisual::Off};
        std::optional<MenuButton> selected_;
    };
}
