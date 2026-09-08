#pragma once

#include "DataBanks.hpp"
#include "Display.hpp"
#include "IBarBankPlayback.hpp"
#include "IBarCardPlayback.hpp"
#include "IBarCameraButtonPlayback.hpp"
#include "IBarCurrentPlayerPlayback.hpp"
#include "IBarJailCardPlayback.hpp"
#include "IBarLayout.hpp"
#include "IBarPropertyPlayback.hpp"
#include "IBarRuleState.hpp"
#include "IBarScoreStripPlayback.hpp"
#include "RuleTypes.hpp"
#include "SequencePlayback.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
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

    struct RuleActionHitState
    {
        layout::ActionButtonLayout layout{layout::ActionButtonLayout::General};
        layout::ActionButtonMask activeSlots{};
    };


    struct ActionButtonInputs
    {
        display::Screen2D desired2DView{display::Screen2D::Main};
        RuleMode ruleMode{RuleMode::Nothing};
        rules::PlayerNumber rulePlayer{rules::NobodyPlayer};
        bool trackRules{true};
        bool gameInProgress{};
        bool tradeEligible{};
        bool rollDiceDesired{};
        bool raiseCashCanBankrupt{};
        bool canBuild{};
        bool canSell{};
        bool canMortgage{};
        bool canUnmortgage{};
        bool aiButtonRemoteState{};
        bool bankHovered{};
        std::optional<std::uint8_t> pressedButtonIndex;
        std::optional<std::uint8_t> desiredCardIndex;
        std::optional<std::uint8_t> desiredBuyAuctionSquare;
        pieces::BoardCameraView desiredBoardCamera{pieces::BoardCameraView::TopDownSoccer};
        ScoreStripPlan scoreStrip{};
        PropertyTitlePlan propertyTitles{};
        int propertyCurrentMouseOver{-1};
        std::uint64_t tick{};
    };

    [[nodiscard]] RuleActionHitState ruleActionHitState(
        bool visible,
        rules::PlayerNumber activePlayer,
        const ActionButtonInputs& inputs) noexcept;


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
            propertyTitles_.reset();
            propertyHover_.reset();
            card_.reset();
            jailCards_.reset();
            buyAuctionPopup_.reset();
            scoreStrip_.reset();
            consumedPressedButton_.reset();
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

        [[nodiscard]] std::optional<std::uint8_t> consumedPressedButton() const noexcept
        {
            return consumedPressedButton_;
        }

        [[nodiscard]] data::DataId propertyHoverDeed() const noexcept
        {
            return propertyHover_.currentDeed();
        }

        [[nodiscard]] CardVisualState cardVisualState() const noexcept
        {
            return card_.visualState();
        }

        [[nodiscard]] data::DataId currentCardSequence() const noexcept
        {
            return card_.currentSequence();
        }

        [[nodiscard]] data::DataId jailCardCurrent(std::size_t deck) const noexcept
        {
            return jailCards_.current(deck);
        }

        [[nodiscard]] data::DataId buyAuctionPopupDeed() const noexcept
        {
            return buyAuctionPopup_.currentDeed();
        }

        [[nodiscard]] bool buyAuctionPopupOnLeft() const noexcept
        {
            return buyAuctionPopup_.onLeft();
        }

        [[nodiscard]] const ScoreStripPlayerRuntime& scorePlayerState(
            rules::PlayerNumber player) const noexcept
        {
            return scoreStrip_.playerState(player);
        }

        [[nodiscard]] const ScoreTextState& scoreTextState(
            rules::PlayerNumber player) const noexcept
        {
            return scoreStrip_.textState(player);
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
        std::optional<std::uint8_t> consumedPressedButton_;
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
        PropertyTitlePlayback propertyTitles_;
        PropertyHoverPlayback propertyHover_;
        CardPlayback card_;
        JailCardPlayback jailCards_;
        BuyAuctionPopupPlayback buyAuctionPopup_;
        ScoreStripPlayback scoreStrip_;
        BankPlayback bank_;
        CurrentPlayerPlayback currentPlayer_;
    };
}
