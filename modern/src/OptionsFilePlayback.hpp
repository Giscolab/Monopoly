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
    inline constexpr data::DataTag FileTitleTag = 0x023A;
    inline constexpr std::array<data::DataTag, 5> FileButtonInTags{
        0x0241, 0x023E, 0x0244, 0x0238, 0x0235};
    inline constexpr std::uint16_t FileScreenPriority = 50;
    inline constexpr std::uint8_t FileScreenStayAtEnd = 2;

    [[nodiscard]] constexpr data::DataId fileSequence(data::DataTag tag) noexcept
    {
        return data::packDataId(data::LegacyGroupId::LanguageGraphics, tag);
    }

    class FilePlayback final
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
