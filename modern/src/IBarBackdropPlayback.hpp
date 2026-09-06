#pragma once

#include "DataBanks.hpp"
#include "Display.hpp"
#include "IBarBankPlayback.hpp"
#include "IBarCameraButtonPlayback.hpp"
#include "IBarCurrentPlayerPlayback.hpp"
#include "IBarRuleState.hpp"
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
        RuleMode ruleMode{RuleMode::Nothing};
        rules::PlayerNumber rulePlayer{rules::NobodyPlayer};
        bool trackRules{true};
        bool tradeEligible{};
        bool rollDiceDesired{};
        bool raiseCashCanBankrupt{};
        bool canBuild{};
        bool canSell{};
        bool canMortgage{};
        bool canUnmortgage{};
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
            auctionButton_.reset();
            buyButton_.reset();
            cameraButton_.reset();
            doneButton_.reset();
            sellButton_.reset();
            tradeAcceptButton_.reset();
            tradeCounterButton_.reset();
            flatTaxButton_.reset();
            percentageButton_.reset();
            buildButton_.reset();
            bankruptButton_.reset();
            tradeRejectButton_.reset();
            mortgageButton_.reset();
            mainButton_.reset();
            optionsButton_.reset();
            payButton_.reset();
            newGameButton_.reset();
            rollDiceButton_.reset();
            statusButton_.reset();
            tradeButton_.reset();
            unmortgageButton_.reset();
            exitButton_.reset();
            useCardButton_.reset();
            auctionHouseButton_.reset();
            auctionHotelButton_.reset();
            placeHouseButton_.reset();
            placeHotelButton_.reset();
            trackedRuleMode_ = RuleMode::Nothing;
            trackedRulePlayer_ = rules::NobodyPlayer;
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
        CameraButtonPlayback auctionButton_{AuctionButtonIndex};
        CameraButtonPlayback buyButton_{BuyButtonIndex};
        CameraButtonPlayback cameraButton_;
        CameraButtonPlayback doneButton_{DoneButtonIndex};
        CameraButtonPlayback sellButton_{SellButtonIndex};
        CameraButtonPlayback tradeAcceptButton_{TradeAcceptButtonIndex};
        CameraButtonPlayback tradeCounterButton_{TradeCounterButtonIndex};
        CameraButtonPlayback flatTaxButton_{FlatTaxButtonIndex};
        CameraButtonPlayback percentageButton_{PercentageButtonIndex};
        CameraButtonPlayback buildButton_{BuildButtonIndex};
        CameraButtonPlayback bankruptButton_{BankruptButtonIndex};
        CameraButtonPlayback tradeRejectButton_{TradeRejectButtonIndex};
        CameraButtonPlayback mortgageButton_{MortgageButtonIndex};
        CameraButtonPlayback mainButton_{MainButtonIndex};
        CameraButtonPlayback optionsButton_{OptionsButtonIndex};
        CameraButtonPlayback payButton_{PayButtonIndex};
        CameraButtonPlayback newGameButton_{NewGameButtonIndex};
        CameraButtonPlayback rollDiceButton_{RollDiceButtonIndex};
        CameraButtonPlayback statusButton_{StatusButtonIndex};
        CameraButtonPlayback tradeButton_{TradeButtonIndex};
        CameraButtonPlayback unmortgageButton_{UnmortButtonIndex};
        CameraButtonPlayback exitButton_{ExitButtonIndex};
        CameraButtonPlayback useCardButton_{UseCardButtonIndex};
        CameraButtonPlayback auctionHouseButton_{AuctionHouseButtonIndex};
        CameraButtonPlayback auctionHotelButton_{AuctionHotelButtonIndex};
        CameraButtonPlayback placeHouseButton_{PlaceHouseButtonIndex};
        CameraButtonPlayback placeHotelButton_{PlaceHotelButtonIndex};
        RuleMode trackedRuleMode_{RuleMode::Nothing};
        rules::PlayerNumber trackedRulePlayer_{rules::NobodyPlayer};
        BankPlayback bank_;
        CurrentPlayerPlayback currentPlayer_;
    };
}
