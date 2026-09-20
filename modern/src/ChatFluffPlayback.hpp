#pragma once

#include "ChatRuntime.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace monopoly::chat
{
    inline constexpr std::uint16_t ChatFluffButtonPriority = 5230;
    inline constexpr std::uint16_t ChatFluffWindowPriority = 5225;
    inline constexpr data::DataTag ChatFluffButtonTag = 0x00B4;
    inline constexpr data::DataTag ChatFluffUpTag = 0x00A4;
    inline constexpr data::DataTag ChatFluffDownTag = 0x00A3;
    inline constexpr data::DataTag ChatFluffShadeTag = 0x00B0;
    inline constexpr data::DataTag ChatFluffCloseTag = 0x00C2;
    inline constexpr std::array<data::DataTag, 6> ChatFluffCategoryTags{
        0x00B2, 0x00AF, 0x00B1, 0x00C4, 0x00B7, 0x00B5};

    class FluffPlayback final
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
            const State& state,
            engine::SequencePlayback& playback,
            bool bodyControlsInBackground = false);

        void reset() noexcept { current_.clear(); }
        [[nodiscard]] std::size_t objectCount() const noexcept
        {
            return current_.size();
        }

    private:
        std::vector<Published> current_;
    };
}
