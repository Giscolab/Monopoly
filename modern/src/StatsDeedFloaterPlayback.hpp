#pragma once

#include "StatsDeedPlayback.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::statsui
{
    inline constexpr std::uint16_t DeedFloaterPriority = 600;
    inline constexpr std::uint16_t DeedFloaterCardPriority = 602;
    inline constexpr data::DataTag DeedFloaterFrameTag = 0x00C6;
    inline constexpr data::DataTag DeedFloaterCardBaseTag = 0x0CD0;
    inline constexpr int DeedFloaterCardsPerCity = 28;
    inline constexpr int DeedCardHitWidth = 36;
    inline constexpr int DeedCardHitHeight = 42;

    class DeedFloaterPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state, const rules::GameState& gameState,
            const PlayerPlaybackInputs& inputs, int city,
            display::Screen2D desiredView,
            engine::SequencePlayback& playback, bool deedPopupVisible = false);
        void reset() noexcept
        {
            currentDeed_ = data::EmptyDataId;
            currentFrameX_ = 0;
            currentDeedX_ = 0;
        }

    private:
        data::DataId currentDeed_{data::EmptyDataId};
        int currentFrameX_{};
        int currentDeedX_{};
    };
}
