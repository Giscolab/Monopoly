#pragma once

#include "DataBanks.hpp"
#include "DiceIngress.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace monopoly::dice
{
    inline constexpr data::DataTag Fixed2DBaseTag = 0x0096;
    inline constexpr data::DataTag Bobbing2DTag = 0x009C;
    inline constexpr data::DataTag Roll3DBaseTag = 0x0537;
    inline constexpr data::DataTag Idle3DBaseTag = 0x057F;
    inline constexpr std::uint16_t Generic3DPriority = 100;
    inline constexpr std::uint16_t IBarGeneralPriority = 256;

    struct TwoDItem
    {
        data::DataId sequence{data::EmptyDataId};
        std::uint16_t priority{};
        std::int32_t x{};
        std::int32_t y{};
        bool dropFrames{};
        bool loop{};
    };

    struct TwoDPlan
    {
        std::array<std::optional<TwoDItem>, 2> dice;
        bool bobbing{};
    };

    [[nodiscard]] bool validValues(
        const std::array<std::uint8_t, 2>& values) noexcept;
    [[nodiscard]] data::DataId roll3DSequence(
        const std::array<std::uint8_t, 2>& values) noexcept;
    [[nodiscard]] data::DataId idle3DSequence(
        const std::array<std::uint8_t, 2>& values) noexcept;
    [[nodiscard]] TwoDPlan plan2D(
        const std::array<std::uint8_t, 2>& values,
        bool rollAnimationDesired,
        bool iBarVisible) noexcept;

    struct PlaybackUpdate
    {
        bool activeRoll{};
        bool startedRoll{};
        bool cameraTakeover{};
        bool cameraRelease{};
        bool announceRoll{};
        bool queueRelease{};
        bool idleChanged{};
    };

    class Playback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> begin(RollRequest request);
        [[nodiscard]] std::expected<PlaybackUpdate, std::string> tick(
            std::uint64_t tick,
            bool boardVisible,
            bool iBarVisible,
            const rules::GameState& state,
            engine::SequencePlayback& playback);

        [[nodiscard]] bool active() const noexcept { return request_.has_value(); }
        [[nodiscard]] data::DataId current3DSequence() const noexcept { return current3D_; }
        void reset() noexcept;

    private:
        [[nodiscard]] std::expected<bool, std::string> syncIdle(
            bool boardVisible,
            const rules::GameState& state,
            engine::SequencePlayback& playback);

        std::optional<RollRequest> request_;
        data::DataId current3D_{data::EmptyDataId};
        std::uint64_t cameraSetTime_{};
        bool prepared_{};
        bool rollStarted_{};
        bool cameraReleased_{};
    };
}
