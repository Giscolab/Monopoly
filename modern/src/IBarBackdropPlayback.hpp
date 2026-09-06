#pragma once

#include "DataBanks.hpp"
#include "Display.hpp"
#include "IBarBankPlayback.hpp"
#include "IBarCameraButtonPlayback.hpp"
#include "IBarCurrentPlayerPlayback.hpp"
#include "RuleTypes.hpp"
#include "SequencePlayback.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::ibar
{
    inline constexpr data::DataTag PlayerBackdropBaseTag = 0x015B;
    inline constexpr data::DataTag BankBackdropTag = 0x0162;
    inline constexpr std::uint16_t BackdropPriority = 11;
    inline constexpr std::int32_t BackdropX = 0;
    inline constexpr std::int32_t BackdropY = 450;
    inline constexpr std::uint8_t PlayerColourCount = 6;

    [[nodiscard]] std::expected<data::DataId, std::string> desiredBackdrop(
        const rules::GameState& state,
        bool visible,
        rules::PlayerNumber activePlayer);

    struct ActionButtonInputs
    {
        display::Screen2D desired2DView{display::Screen2D::Main};
        bool tradeEligible{};
        bool rollDiceDesired{};
        bool aiButtonRemoteState{};
    };

    class BackdropPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const rules::GameState& state,
            bool visible,
            rules::PlayerNumber activePlayer,
            engine::SequencePlayback& playback,
            ActionButtonInputs inputs = {});

        void reset() noexcept
        {
            currentBackdrop_ = data::EmptyDataId;
            cameraButton_.reset();
            mainButton_.reset();
            optionsButton_.reset();
            rollDiceButton_.reset();
            statusButton_.reset();
            tradeButton_.reset();
            bank_.reset();
            currentPlayer_.reset();
        }

        [[nodiscard]] data::DataId currentBackdrop() const noexcept
        {
            return currentBackdrop_;
        }

        [[nodiscard]] data::DataId currentPlayerToken() const noexcept
        {
            return currentPlayer_.currentToken();
        }

        [[nodiscard]] bool bankVisible() const noexcept
        {
            return bank_.visible();
        }

        [[nodiscard]] CameraButtonVisualState cameraButtonState() const noexcept
        {
            return cameraButton_.visualState();
        }

        [[nodiscard]] CameraButtonVisualState mainButtonState() const noexcept
        {
            return mainButton_.visualState();
        }

        [[nodiscard]] CameraButtonVisualState optionsButtonState() const noexcept
        {
            return optionsButton_.visualState();
        }

        [[nodiscard]] CameraButtonVisualState rollDiceButtonState() const noexcept
        {
            return rollDiceButton_.visualState();
        }

        [[nodiscard]] CameraButtonVisualState statusButtonState() const noexcept
        {
            return statusButton_.visualState();
        }

        [[nodiscard]] CameraButtonVisualState tradeButtonState() const noexcept
        {
            return tradeButton_.visualState();
        }

    private:
        data::DataId currentBackdrop_{data::EmptyDataId};
        CameraButtonPlayback cameraButton_;
        CameraButtonPlayback mainButton_{MainButtonIndex};
        OptionsButtonPlayback optionsButton_;
        CameraButtonPlayback rollDiceButton_{RollDiceButtonIndex};
        CameraButtonPlayback statusButton_{StatusButtonIndex};
        CameraButtonPlayback tradeButton_{TradeButtonIndex};
        BankPlayback bank_;
        CurrentPlayerPlayback currentPlayer_;
    };
}
