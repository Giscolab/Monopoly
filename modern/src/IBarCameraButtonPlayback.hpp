#pragma once

#include "DataBanks.hpp"
#include "SequencePlayback.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::ibar
{
    inline constexpr data::DataTag ButtonBaseTag = 0x008A;
    inline constexpr data::DataTag AIButtonBaseTag = 0x0106;
    inline constexpr std::uint16_t ButtonBasePriority = 999;
    inline constexpr std::uint16_t CameraButtonPriority = ButtonBasePriority;
    inline constexpr std::uint8_t AuctionButtonIndex = 0;
    inline constexpr std::uint8_t BuyButtonIndex = 1;
    inline constexpr std::uint8_t CameraButtonIndex = 2;
    inline constexpr std::uint8_t DoneButtonIndex = 3;
    inline constexpr std::uint8_t SellButtonIndex = 4;
    inline constexpr std::uint8_t FlatTaxButtonIndex = 5;
    inline constexpr std::uint8_t PercentageButtonIndex = 6;
    inline constexpr std::uint8_t BuildButtonIndex = 7;
    inline constexpr std::uint8_t BankruptButtonIndex = 10;
    inline constexpr std::uint8_t MortgageButtonIndex = 12;
    inline constexpr std::uint8_t MainButtonIndex = 13;
    inline constexpr std::uint8_t OptionsButtonIndex = 14;
    inline constexpr std::uint8_t PayButtonIndex = 15;
    inline constexpr std::uint8_t RollDiceButtonIndex = 17;
    inline constexpr std::uint8_t StatusButtonIndex = 18;
    inline constexpr std::uint8_t TradeButtonIndex = 19;
    inline constexpr std::uint8_t UnmortButtonIndex = 20;
    inline constexpr std::uint8_t UseCardButtonIndex = 23;
    inline constexpr std::uint8_t AuctionHouseButtonIndex = 24;
    inline constexpr std::uint8_t AuctionHotelButtonIndex = 25;
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

    [[nodiscard]] constexpr std::uint16_t actionButtonPriority(
        std::uint8_t buttonIndex) noexcept
    {
        switch (buttonIndex)
        {
        case BuildButtonIndex:
        case UnmortButtonIndex:
            return ButtonBasePriority + 1;
        case AuctionButtonIndex:
        case SellButtonIndex:
        case PercentageButtonIndex:
        case BankruptButtonIndex:
        case MortgageButtonIndex:
        case PayButtonIndex:
        case UseCardButtonIndex:
            return ButtonBasePriority + 2;
        case DoneButtonIndex:
        case BuyButtonIndex:
        case FlatTaxButtonIndex:
        case RollDiceButtonIndex:
        case AuctionHouseButtonIndex:
        case AuctionHotelButtonIndex:
            return ButtonBasePriority + 3;
        default:
            return ButtonBasePriority;
        }
    }

    [[nodiscard]] constexpr data::DataId actionButtonSequence(
        std::uint8_t buttonIndex,
        CameraButtonVisualState state,
        bool useGreyButtons = false) noexcept
    {
        if (state == CameraButtonVisualState::Off)
            return data::EmptyDataId;
        const auto mode = static_cast<std::uint8_t>(state);
        const auto baseTag = useGreyButtons ? AIButtonBaseTag : ButtonBaseTag;
        return data::packDataId(
            data::LegacyGroupId::LanguageGraphics,
            static_cast<data::DataTag>(baseTag +
                buttonIndex * ButtonAnimationsPerSet + mode));
    }

    [[nodiscard]] constexpr data::DataId cameraButtonSequence(
        CameraButtonVisualState state) noexcept
    {
        return actionButtonSequence(CameraButtonIndex, state);
    }

    [[nodiscard]] constexpr data::DataId optionsButtonSequence(
        CameraButtonVisualState state) noexcept
    {
        return actionButtonSequence(OptionsButtonIndex, state);
    }

    class CameraButtonPlayback final
    {
    public:
        explicit CameraButtonPlayback(
            std::uint8_t buttonIndex = CameraButtonIndex) noexcept
            : buttonIndex_(buttonIndex),
              priority_(actionButtonPriority(buttonIndex))
        {
        }

        [[nodiscard]] std::expected<void, std::string> sync(
            bool desired,
            engine::SequencePlayback& playback,
            bool useGreyButtons = false,
            bool buttonBarStable = true);

        void reset() noexcept
        {
            visualState_ = CameraButtonVisualState::Off;
            currentSequence_ = data::EmptyDataId;
            currentGrey_ = false;
        }

        [[nodiscard]] CameraButtonVisualState visualState() const noexcept
        {
            return visualState_;
        }

        [[nodiscard]] data::DataId currentSequence() const noexcept
        {
            return currentSequence_;
        }

        [[nodiscard]] std::uint16_t priority() const noexcept
        {
            return priority_;
        }

        [[nodiscard]] bool currentGrey() const noexcept
        {
            return currentGrey_;
        }

    private:
        std::uint8_t buttonIndex_{CameraButtonIndex};
        std::uint16_t priority_{ButtonBasePriority};
        CameraButtonVisualState visualState_{CameraButtonVisualState::Off};
        data::DataId currentSequence_{data::EmptyDataId};
        bool currentGrey_{};
    };

    class OptionsButtonPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            bool desired,
            engine::SequencePlayback& playback,
            bool buttonBarStable = true)
        {
            return core_.sync(desired, playback, false, buttonBarStable);
        }

        void reset() noexcept
        {
            core_.reset();
        }

        [[nodiscard]] CameraButtonVisualState visualState() const noexcept
        {
            return core_.visualState();
        }

        [[nodiscard]] data::DataId currentSequence() const noexcept
        {
            return core_.currentSequence();
        }

    private:
        CameraButtonPlayback core_{OptionsButtonIndex};
    };
}
