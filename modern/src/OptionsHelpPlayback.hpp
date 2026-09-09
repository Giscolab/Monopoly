#pragma once

#include "DataBanks.hpp"
#include "Display.hpp"
#include "OptionsUI.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::optionsui
{
    inline constexpr data::DataTag HelpTitleTag = 0x0249;
    inline constexpr std::array<data::DataTag, 3> HelpButtonInTags{
        0x0250, 0x024B, 0x0235};
    inline constexpr std::uint16_t HelpScreenPriority = 50;
    inline constexpr std::uint8_t HelpScreenStayAtEnd = 2;

    [[nodiscard]] constexpr data::DataId helpSequence(data::DataTag tag) noexcept
    {
        return data::packDataId(data::LegacyGroupId::LanguageGraphics, tag);
    }

    class HelpPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            display::Screen2D desiredView,
            engine::SequencePlayback& playback);

        void reset() noexcept { visible_ = false; }
        [[nodiscard]] bool visible() const noexcept { return visible_; }

    private:
        bool visible_{};
    };
}
