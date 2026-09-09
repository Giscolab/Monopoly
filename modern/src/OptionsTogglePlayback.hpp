#pragma once

#include "OptionsUI.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::optionsui
{
    inline constexpr data::DataTag ToggleOffUnselectedTag = 0x026B;
    inline constexpr data::DataTag ToggleOffSelectedTag = 0x026C;
    inline constexpr data::DataTag ToggleOnUnselectedTag = 0x0272;
    inline constexpr data::DataTag ToggleOnSelectedTag = 0x0273;
    inline constexpr std::uint8_t ToggleStayAtEnd = 2;

    [[nodiscard]] constexpr std::uint16_t togglePriority(
        OptionToggle toggle) noexcept
    {
        return static_cast<std::uint16_t>(50U +
            static_cast<std::uint8_t>(toggle));
    }

    [[nodiscard]] constexpr data::DataTag toggleTag(
        bool onSide, bool value) noexcept
    {
        if (onSide)
            return value ? ToggleOnSelectedTag : ToggleOnUnselectedTag;
        return value ? ToggleOffUnselectedTag : ToggleOffSelectedTag;
    }

    [[nodiscard]] constexpr data::DataId toggleSequence(
        bool onSide, bool value) noexcept
    {
        return data::packDataId(data::LegacyGroupId::LanguageGraphics,
            toggleTag(onSide, value));
    }

    class TogglePlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            display::Screen2D desiredView,
            engine::SequencePlayback& playback);

        [[nodiscard]] std::int8_t shown(OptionToggle toggle) const noexcept;
        void reset() noexcept { shown_.fill(-1); }

    private:
        std::array<std::int8_t, SupportedOptionToggles.size()> shown_{{-1, -1, -1, -1}};
    };
}
