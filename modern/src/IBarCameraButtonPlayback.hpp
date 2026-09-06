#pragma once

#include "DataBanks.hpp"
#include "SequencePlayback.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::ibar
{
    inline constexpr data::DataTag ButtonBaseTag = 0x008A;
    inline constexpr std::uint16_t CameraButtonPriority = 999;
    inline constexpr std::uint8_t CameraButtonIndex = 2;
    inline constexpr std::uint8_t ButtonAnimationsPerSet = 4;
    inline constexpr std::uint8_t CameraButtonStayAtEnd = 2;
    inline constexpr std::uint8_t CameraButtonLoopToBeginning = 3;

    enum class CameraButtonVisualState : std::int8_t
    {
        Off = -1,
        In = 0,
        Idle = 1,
        Out = 2,
        Pressed = 3
    };

    [[nodiscard]] constexpr data::DataId cameraButtonSequence(
        CameraButtonVisualState state) noexcept
    {
        if (state == CameraButtonVisualState::Off)
            return data::EmptyDataId;
        const auto mode = static_cast<std::uint8_t>(state);
        return data::packDataId(
            data::LegacyGroupId::LanguageGraphics,
            static_cast<data::DataTag>(ButtonBaseTag +
                CameraButtonIndex * ButtonAnimationsPerSet + mode));
    }

    class CameraButtonPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            bool iBarVisible,
            engine::SequencePlayback& playback);

        void reset() noexcept
        {
            visualState_ = CameraButtonVisualState::Off;
            currentSequence_ = data::EmptyDataId;
        }

        [[nodiscard]] CameraButtonVisualState visualState() const noexcept
        {
            return visualState_;
        }

        [[nodiscard]] data::DataId currentSequence() const noexcept
        {
            return currentSequence_;
        }

    private:
        CameraButtonVisualState visualState_{CameraButtonVisualState::Off};
        data::DataId currentSequence_{data::EmptyDataId};
    };
}
