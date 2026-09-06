#pragma once

#include "DataBanks.hpp"
#include "SequencePlayback.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::ibar
{
    inline constexpr data::DataTag BankSequenceTag = 0x02F5;
    inline constexpr std::uint16_t BankPriority = 256;
    inline constexpr std::int32_t BankX = 755;
    inline constexpr std::int32_t BankY = 560;

    [[nodiscard]] constexpr data::DataId bankSequence() noexcept
    {
        return data::packDataId(
            data::LegacyGroupId::LanguageGraphics,
            BankSequenceTag);
    }

    class BankPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            bool visible,
            bool hovered,
            engine::SequencePlayback& playback);

        void reset() noexcept
        {
            visible_ = false;
            hovered_ = false;
        }

        [[nodiscard]] bool visible() const noexcept
        {
            return visible_;
        }

        [[nodiscard]] bool hovered() const noexcept
        {
            return hovered_;
        }

    private:
        bool visible_{};
        bool hovered_{};
    };
}
