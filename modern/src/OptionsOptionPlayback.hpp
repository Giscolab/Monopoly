#pragma once

#include "OptionsUI.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::optionsui
{
    inline constexpr std::uint16_t OptionScreenPriority = 50;
    inline constexpr std::uint8_t OptionScreenStayAtEnd = 2;

    inline constexpr data::DataTag OptionTitleTag = 0x0277;
    inline constexpr data::DataTag OptionSoundSubtitleTag = 0x0278;
    inline constexpr data::DataTag OptionDisplaySubtitleTag = 0x0262;
    inline constexpr data::DataTag OptionOkayInTag = 0x026E;

    inline constexpr int OptionOkayX = 350;
    inline constexpr int OptionOkayY = 450;
    inline constexpr int OptionOkayWidth = 127;
    inline constexpr int OptionOkayHeight = 36;

    [[nodiscard]] constexpr data::DataId optionSequence(data::DataTag tag) noexcept
    {
        return data::packDataId(data::LegacyGroupId::LanguageGraphics, tag);
    }

    inline constexpr std::array<data::DataTag, 4> OptionScreenTags{
        OptionTitleTag,
        OptionSoundSubtitleTag,
        OptionDisplaySubtitleTag,
        OptionOkayInTag
    };

    class OptionPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            display::Screen2D desiredView,
            engine::SequencePlayback& playback);

        [[nodiscard]] bool visible() const noexcept { return visible_; }
        void reset() noexcept { visible_ = false; }

    private:
        bool visible_{};
    };
}
