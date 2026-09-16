#pragma once

#include "DataBanks.hpp"
#include "IBar.hpp"
#include "SequencePlayback.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::ibar
{
    inline constexpr std::uint16_t EscapeMenuPriority = 5000;
    inline constexpr data::DataTag EscapeAskUsaTag = 0x02F4;
    inline constexpr data::DataTag EscapeNoUsaTag = 0x0E4F;
    inline constexpr data::DataTag EscapeYesUsaTag = 0x16BA;
    inline constexpr data::DataTag EscapeAskEuropeTag = 0x0481;
    inline constexpr data::DataTag EscapeNoEuropeTag = 0x111D;
    inline constexpr data::DataTag EscapeYesEuropeTag = 0x199C;

    class EscapeConfirmationPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state, bool usaEdition,
            engine::SequencePlayback& playback);

        void reset() noexcept { visible_ = false; usaEdition_ = true; }

    private:
        bool visible_{};
        bool usaEdition_{true};
    };
}
