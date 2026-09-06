#pragma once

#include "DataBanks.hpp"
#include "Display.hpp"
#include "SequencePlayback.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace monopoly::ibar
{
    inline constexpr std::uint16_t CardPriority = 1005;
    inline constexpr std::int32_t CardX = 0;
    inline constexpr std::int32_t CardYMain = 0;
    inline constexpr std::int32_t CardYNotMain = 136;
    inline constexpr std::uint8_t CardEndingStop = 1;
    inline constexpr std::uint8_t CardEndingStayAtEnd = 2;
    inline constexpr std::uint8_t CardsPerDeck = 16;

    inline constexpr data::DataTag ChanceDeckOutBaseTag = 0x000F;
    inline constexpr data::DataTag CommunityDeckOutBaseTag = 0x0036;
    inline constexpr data::DataTag CommunityFaceBaseTag = 0x0008;
    inline constexpr data::DataTag ChanceFaceBaseTag = 0x0018;
    inline constexpr data::DataTag ChanceIdleBaseTag = 0x0028;
    inline constexpr data::DataTag ChanceCardInBaseTag = 0x0038;
    inline constexpr data::DataTag ChanceCardOutBaseTag = 0x0048;
    inline constexpr data::DataTag CommunityIdleBaseTag = 0x0059;
    inline constexpr data::DataTag CommunityCardInBaseTag = 0x0069;
    inline constexpr data::DataTag CommunityCardOutBaseTag = 0x0079;

    enum class CardVisualState : std::uint8_t
    {
        Off,
        DeckOut,
        CardIn,
        FaceIn,
        Idle,
        Out
    };

    [[nodiscard]] data::DataId cardSequence(
        CardVisualState state,
        std::uint8_t cardIndex,
        pieces::BoardCameraView camera) noexcept;

    class CardPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            std::optional<std::uint8_t> desiredCard,
            bool iBarVisible,
            display::Screen2D view,
            pieces::BoardCameraView camera,
            engine::SequencePlayback& playback);
        void reset() noexcept;

        [[nodiscard]] CardVisualState visualState() const noexcept
        {
            return visualState_;
        }
        [[nodiscard]] data::DataId currentSequence() const noexcept
        {
            return currentSequence_;
        }
        [[nodiscard]] std::optional<std::uint8_t> lastCard() const noexcept
        {
            return lastCard_;
        }

    private:
        CardVisualState visualState_{CardVisualState::Off};
        data::DataId currentSequence_{data::EmptyDataId};
        std::optional<std::uint8_t> lastCard_;
    };
}
