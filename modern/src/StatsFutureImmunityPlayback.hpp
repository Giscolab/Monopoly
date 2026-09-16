#pragma once

#include "SequencePlayback.hpp"
#include "StatsFutureImmunityUI.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace monopoly::statsui
{
    inline constexpr std::uint16_t FutureImmunityPopupPriority = 610;
    inline constexpr std::uint16_t FutureImmunityContentPriority = 611;
    inline constexpr std::uint16_t FutureImmunityArrowPriority = 620;
    inline constexpr data::DataTag FutureImmunityFrameTag = 0x02D0;
    inline constexpr data::DataTag FutureImmunityBoardTag = 0x1159;
    inline constexpr data::DataTag FutureImmunityDownTag = 0x0007;
    inline constexpr data::DataTag FutureImmunityUpTag = 0x0009;
    class FutureImmunityPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const FutureImmunityState& state,
            display::Screen2D desiredView,
            engine::SequencePlayback& playback);
        void reset() noexcept;

    private:
        struct Published
        {
            data::DataId id{data::EmptyDataId};
            std::uint16_t priority{};
            friend bool operator==(const Published&, const Published&) = default;
        };
        static constexpr std::size_t SlotCount = 4;
        std::array<std::optional<Published>, SlotCount> published_{};
    };
}
