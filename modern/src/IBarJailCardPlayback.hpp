#pragma once

#include "DataBanks.hpp"
#include "RuleTypes.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::ibar
{
    inline constexpr data::DataTag JailCardBaseTag = 0x0992;
    inline constexpr std::uint16_t JailCardBasePriority = 256;
    inline constexpr std::array<std::int32_t, 2> JailCardX{740, 750};
    inline constexpr std::array<std::int32_t, 2> JailCardY{506, 525};

    [[nodiscard]] constexpr data::DataId jailCardSequence(std::size_t deck) noexcept
    {
        return deck < 2
            ? data::packDataId(data::LegacyGroupId::LanguageGraphics,
                static_cast<data::DataTag>(JailCardBaseTag + deck))
            : data::EmptyDataId;
    }

    class JailCardPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const rules::GameState& state,
            bool propertyBarAvailable,
            rules::PlayerNumber activePlayer,
            engine::SequencePlayback& playback);

        void reset() noexcept { current_.fill(data::EmptyDataId); }
        [[nodiscard]] data::DataId current(std::size_t deck) const noexcept
        {
            return deck < current_.size() ? current_[deck] : data::EmptyDataId;
        }

    private:
        std::array<data::DataId, 2> current_{};
    };
}
