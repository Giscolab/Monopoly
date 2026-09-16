#pragma once

#include "ChatRuntime.hpp"
#include "SequencePlayback.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace monopoly::chat
{
    inline constexpr std::uint16_t ChatOptionButtonPriority = 5230;
    inline constexpr std::uint16_t ChatOptionPanelPriority = 5232;
    inline constexpr data::DataTag ChatOptionButtonTag = 0x00B6;
    inline constexpr data::DataTag ChatOptionPanelTag = 0x00C5;

    class OptionPlayback final
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
            engine::SequencePlayback& playback);

        void reset() noexcept { current_.clear(); }
        [[nodiscard]] std::size_t objectCount() const noexcept
        {
            return current_.size();
        }

    private:
        std::vector<Published> current_;
    };
}
