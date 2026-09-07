#include "IBarBackdropPlayback.hpp"
#include "RuntimeState.hpp"

#include <array>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::ibar
{
    std::expected<data::DataId, std::string> desiredBackdrop(
        const rules::GameState& state,
        bool visible,
        rules::PlayerNumber activePlayer)
    {
        if (!visible)
        {
            return data::EmptyDataId;
        }

        if (activePlayer == rules::BankPlayer)
        {
            return data::packDataId(
                data::LegacyGroupId::Main,
                BankBackdropTag);
        }

        if (activePlayer >= rules::MaxPlayers ||
            activePlayer >= state.numberOfPlayers)
        {
            return data::EmptyDataId;
        }

        const auto colour = state.players[activePlayer].colour;
        if (colour >= PlayerColourCount)
        {
            return std::unexpected("IBar player colour is outside the legacy 0..5 range");
        }

        return data::packDataId(
            data::LegacyGroupId::Main,
            static_cast<data::DataTag>(
                PlayerBackdropBaseTag + colour));
    }

    RuleActionHitState ruleActionHitState(
        bool visible,
        rules::PlayerNumber activePlayer,
        const ActionButtonInputs& inputs) noexcept
    {
        RuleActionHitState result{};
        if (!visible || activePlayer > rules::BankPlayer)
            return result;

        const auto add = [&](layout::ActionButtonSlot slot)
        {
            result.activeSlots |= layout::actionButtonBit(slot);
        };

        switch (inputs.ruleMode)
        {
        case RuleMode::BuyAuction:
            result.layout = layout::ActionButtonLayout::BuyAuction;
            add(layout::ActionButtonSlot::Main);
            add(layout::ActionButtonSlot::General3);
            break;
        case RuleMode::TaxDecision:
            result.layout = layout::ActionButtonLayout::TaxDecision;
            add(layout::ActionButtonSlot::Main);
            add(layout::ActionButtonSlot::General3);
            break;
        case RuleMode::Trading:
            result.layout = layout::ActionButtonLayout::Trading;
            add(layout::ActionButtonSlot::Main);
            add(layout::ActionButtonSlot::General2);
            add(layout::ActionButtonSlot::General3);
            break;
        case RuleMode::JailExitPCR:
            add(layout::ActionButtonSlot::Main);
            add(layout::ActionButtonSlot::General2);
            add(layout::ActionButtonSlot::General3);
            break;
        case RuleMode::JailExitPXR:
            add(layout::ActionButtonSlot::Main);
            add(layout::ActionButtonSlot::General2);
            break;
        case RuleMode::JailExitPCX:
            add(layout::ActionButtonSlot::General2);
            add(layout::ActionButtonSlot::General3);
            break;
        case RuleMode::JailExitPXX:
            add(layout::ActionButtonSlot::General2);
            break;
        case RuleMode::RaiseMoney:
            if (inputs.canSell) add(layout::ActionButtonSlot::General2);
            if (inputs.canMortgage) add(layout::ActionButtonSlot::General3);
            if (inputs.raiseCashCanBankrupt) add(layout::ActionButtonSlot::Main);
            break;
        case RuleMode::HotelDecomposition:
            add(layout::ActionButtonSlot::General2);
            break;
        case RuleMode::HousingShort:
        case RuleMode::HotelShort:
        case RuleMode::ViewingCard:
            add(layout::ActionButtonSlot::Main);
            break;
        case RuleMode::GameOver:
            add(layout::ActionButtonSlot::General2);
            add(layout::ActionButtonSlot::Main);
            break;
        case RuleMode::StartTurn:
            if (inputs.canBuild) add(layout::ActionButtonSlot::General1);
            if (inputs.canSell) add(layout::ActionButtonSlot::General2);
            if (inputs.canMortgage) add(layout::ActionButtonSlot::General3);
            if (inputs.canUnmortgage) add(layout::ActionButtonSlot::General4);
            if (inputs.rollDiceDesired) add(layout::ActionButtonSlot::Main);
            break;
        case RuleMode::OtherPlayer:
        case RuleMode::DoneTurn:
        case RuleMode::FreeUnmortgage:
            if (inputs.canBuild) add(layout::ActionButtonSlot::General1);
            if (inputs.canSell) add(layout::ActionButtonSlot::General2);
            if (inputs.canMortgage) add(layout::ActionButtonSlot::General3);
            if (inputs.canUnmortgage) add(layout::ActionButtonSlot::General4);
            add(layout::ActionButtonSlot::Main);
            break;
        case RuleMode::DeedActive:
            if (inputs.canBuild) add(layout::ActionButtonSlot::General1);
            if (inputs.canSell) add(layout::ActionButtonSlot::General2);
            if (inputs.canMortgage) add(layout::ActionButtonSlot::General3);
            if (inputs.canUnmortgage) add(layout::ActionButtonSlot::General4);
            add(layout::ActionButtonSlot::Main);
            break;
        case RuleMode::Build:
        case RuleMode::Sell:
        case RuleMode::Mortgage:
        case RuleMode::UnMortgage:
        case RuleMode::OtherPlayerRemote:
            add(layout::ActionButtonSlot::Main);
            break;
        default:
            break;
        }
        return result;
    }

    std::expected<void, std::string> BackdropPlayback::sync(
        const rules::GameState& state,
        bool visible,
        rules::PlayerNumber activePlayer,
        engine::SequencePlayback& playback,
        ActionButtonInputs inputs)
    {
        consumedPressedButton_.reset();
        const auto resolved = desiredBackdrop(state, visible, activePlayer);
        if (!resolved)
        {
            return std::unexpected(resolved.error());
        }

        const data::DataId desired = *resolved;
        if (desired != currentBackdrop_)
        {
        std::shared_ptr<const sequence::SequenceProgram> program;
        if (desired != data::EmptyDataId)
        {
            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), desired);
            if (!loaded)
            {
                return std::unexpected(loaded.error().detail);
            }
            program = std::move(*loaded);
        }

        std::vector<sequence::SequenceCommand> commands;
        if (currentBackdrop_ != data::EmptyDataId)
        {
            commands.push_back(sequence::StopSequenceCommand{
                currentBackdrop_, BackdropPriority, false});
        }

        if (desired != data::EmptyDataId)
        {
            commands.push_back(sequence::StartSequenceCommand{
                std::move(program), BackdropPriority});
            commands.push_back(sequence::makeMoveXY(
                desired, BackdropPriority, BackdropX, BackdropY));
        }

        if (commands.size() >
            sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit IBar backdrop transition");
        }

        for (auto& command : commands)
        {
            const auto queued = std::visit(
                [&](auto value)
                {
                    return playback.commands().enqueue(std::move(value));
                },
                std::move(command));

            if (!queued)
            {
                return std::unexpected(
                    "validated IBar backdrop command rejected");
            }
        }

        currentBackdrop_ = desired;
        }

        // UDIBar.cpp computes IBarIsStable once before iterating buttons.
        // Off/Idle are stable; In/Out/Pressed block ordinary fly-in/fly-out
        // transitions until the next stable cycle.
        const auto stable = [](CameraButtonVisualState state) noexcept
        {
            return state == CameraButtonVisualState::Off ||
                state == CameraButtonVisualState::Idle;
        };

        const auto wantsDone = [](RuleMode mode) noexcept
        {
            switch (mode)
            {
            case RuleMode::Build:
            case RuleMode::Sell:
            case RuleMode::Mortgage:
            case RuleMode::UnMortgage:
            case RuleMode::OtherPlayer:
            case RuleMode::OtherPlayerRemote:
            case RuleMode::DoneTurn:
            case RuleMode::DeedActive:
            case RuleMode::ViewingCard:
            case RuleMode::FreeUnmortgage:
                return true;
            default:
                return false;
            }
        };
        const auto wantsUseCard = [](RuleMode mode) noexcept
        {
            return mode == RuleMode::JailExitPCR ||
                mode == RuleMode::JailExitPCX;
        };
        const auto wantsJailRoll = [](RuleMode mode) noexcept
        {
            return mode == RuleMode::JailExitPCR ||
                mode == RuleMode::JailExitPXR;
        };
        const auto wantsPay = [](RuleMode mode) noexcept
        {
            return mode == RuleMode::JailExitPCR ||
                mode == RuleMode::JailExitPXR ||
                mode == RuleMode::JailExitPCX ||
                mode == RuleMode::JailExitPXX;
        };

        const bool gameInProgress = runtime::state().gameInProgress;
        const bool buyAuction = inputs.ruleMode == RuleMode::BuyAuction;
        const bool taxDecision = inputs.ruleMode == RuleMode::TaxDecision;
        const bool raiseMoney = inputs.ruleMode == RuleMode::RaiseMoney;
        const bool bssmRulePlayerActive = activePlayer == inputs.rulePlayer;
        const bool trading =
            inputs.ruleMode == RuleMode::Trading && bssmRulePlayerActive;
        const bool hotelDecomposition =
            inputs.ruleMode == RuleMode::HotelDecomposition && bssmRulePlayerActive;
        const bool placeHouse =
            inputs.ruleMode == RuleMode::PlaceHouse && bssmRulePlayerActive;
        const bool placeHotel =
            inputs.ruleMode == RuleMode::PlaceHotel && bssmRulePlayerActive;
        const bool housingShort =
            inputs.ruleMode == RuleMode::HousingShort && bssmRulePlayerActive;
        const bool hotelShort =
            inputs.ruleMode == RuleMode::HotelShort && bssmRulePlayerActive;
        const bool gameOver = inputs.ruleMode == RuleMode::GameOver;
        const bool bssmNormal =
            inputs.ruleMode == RuleMode::StartTurn ||
            inputs.ruleMode == RuleMode::OtherPlayer ||
            inputs.ruleMode == RuleMode::DoneTurn ||
            inputs.ruleMode == RuleMode::FreeUnmortgage;
        const bool bssmRaiseMoney = inputs.ruleMode == RuleMode::RaiseMoney;
        const bool deedActive = inputs.ruleMode == RuleMode::DeedActive;
        const bool buildDesired = (bssmNormal || deedActive) && inputs.canBuild;
        const bool sellDesired =
            (bssmNormal || bssmRaiseMoney || deedActive) && inputs.canSell;
        const bool mortgageDesired =
            (bssmNormal || bssmRaiseMoney || deedActive) && inputs.canMortgage;
        const bool unmortgageDesired =
            (bssmNormal || deedActive) && inputs.canUnmortgage;

        struct ButtonRequest
        {
            CameraButtonPlayback* button;
            bool desired;
            bool useGrey;
        };

        // Preserve the original t=0..IBAR_BUTTON_DISTINCT_MAX iteration order
        // for the subset whose predicates are now ported.
        const std::array requests{
            ButtonRequest{&auctionButton_, visible && buyAuction, inputs.aiButtonRemoteState},
            ButtonRequest{&buyButton_, visible && buyAuction, inputs.aiButtonRemoteState},
            ButtonRequest{&cameraButton_, visible, false},
            ButtonRequest{&doneButton_, visible && wantsDone(inputs.ruleMode), inputs.aiButtonRemoteState},
            ButtonRequest{&sellButton_, visible && (hotelDecomposition || sellDesired), inputs.aiButtonRemoteState},
            ButtonRequest{&flatTaxButton_, visible && taxDecision, inputs.aiButtonRemoteState},
            ButtonRequest{&percentageButton_, visible && taxDecision, inputs.aiButtonRemoteState},
            ButtonRequest{&buildButton_, visible && buildDesired, inputs.aiButtonRemoteState},
            ButtonRequest{&tradeAcceptButton_, visible && trading, inputs.aiButtonRemoteState},
            ButtonRequest{&tradeCounterButton_, visible && trading, inputs.aiButtonRemoteState},
            ButtonRequest{&bankruptButton_, visible && raiseMoney &&
                inputs.raiseCashCanBankrupt, inputs.aiButtonRemoteState},
            ButtonRequest{&tradeRejectButton_, visible && trading, inputs.aiButtonRemoteState},
            ButtonRequest{&mortgageButton_, visible && mortgageDesired, inputs.aiButtonRemoteState},
            ButtonRequest{&mainButton_, visible && gameInProgress &&
                (inputs.desired2DView == display::Screen2D::Portfolio ||
                 inputs.desired2DView == display::Screen2D::Trade), false},
            ButtonRequest{&optionsButton_, visible && gameInProgress, false},
            ButtonRequest{&payButton_, visible && wantsPay(inputs.ruleMode), inputs.aiButtonRemoteState},
            ButtonRequest{&newGameButton_, visible && gameOver, false},
            ButtonRequest{&rollDiceButton_, visible &&
                (inputs.rollDiceDesired || wantsJailRoll(inputs.ruleMode)),
                inputs.aiButtonRemoteState},
            ButtonRequest{&statusButton_, visible && gameInProgress &&
                inputs.desired2DView == display::Screen2D::Main, false},
            ButtonRequest{&tradeButton_, visible && gameInProgress &&
                inputs.tradeEligible && inputs.desired2DView != display::Screen2D::Trade, false},
            ButtonRequest{&unmortgageButton_, visible && unmortgageDesired, inputs.aiButtonRemoteState},
            ButtonRequest{&exitButton_, visible && gameOver, false},
            ButtonRequest{&useCardButton_, visible && wantsUseCard(inputs.ruleMode),
                inputs.aiButtonRemoteState},
            ButtonRequest{&auctionHouseButton_, visible && housingShort, inputs.aiButtonRemoteState},
            ButtonRequest{&auctionHotelButton_, visible && hotelShort, inputs.aiButtonRemoteState},
            ButtonRequest{&placeHouseButton_, visible && placeHouse, inputs.aiButtonRemoteState},
            ButtonRequest{&placeHotelButton_, visible && placeHotel, inputs.aiButtonRemoteState}
        };

        bool actionButtonsStable = true;
        for (const auto& request : requests)
            actionButtonsStable = actionButtonsStable &&
                stable(request.button->visualState());

        const bool justChanged = inputs.trackRules &&
            (trackedRuleMode_ != inputs.ruleMode ||
             trackedRulePlayer_ != inputs.rulePlayer);
        if (inputs.trackRules)
        {
            trackedRuleMode_ = inputs.ruleMode;
            trackedRulePlayer_ = inputs.rulePlayer;
        }

        // IBAR_JustChanged makes this pass outgoing-only. It is cleared at the
        // end of the original show routine, so the next sync may fly buttons in
        // once every existing animation is stable again.
        std::optional<std::uint8_t> consumedPressed;
        for (const auto& request : requests)
        {
            const auto before = request.button->visualState();
            const auto buttonIndex = request.button->buttonIndex();
            const bool requestPressed = inputs.pressedButtonIndex == buttonIndex;
            const auto result = request.button->sync(
                request.desired, playback, request.useGrey,
                actionButtonsStable, !justChanged, requestPressed);
            if (!result)
                return result;
            if (requestPressed &&
                (before == CameraButtonVisualState::Pressed ||
                 request.button->visualState() == CameraButtonVisualState::Pressed))
            {
                consumedPressed = buttonIndex;
            }
        }

        const auto desiredCard = inputs.ruleMode == RuleMode::ViewingCard
            ? inputs.desiredCardIndex : std::optional<std::uint8_t>{};
        const auto card = card_.sync(
            desiredCard, visible, inputs.desired2DView,
            inputs.desiredBoardCamera, playback);
        if (!card)
            return card;

        const auto propertyTitles = propertyTitles_.sync(
            inputs.propertyTitles, playback);
        if (!propertyTitles)
            return propertyTitles;

        const bool propertyBarAvailable = visible &&
            (inputs.desired2DView == display::Screen2D::Main ||
             inputs.desired2DView == display::Screen2D::Trade);
        const auto jailCards = jailCards_.sync(
            state, propertyBarAvailable, activePlayer, playback);
        if (!jailCards)
            return jailCards;

        const int ruleCurrentSquare =
            state.currentPlayer < state.numberOfPlayers &&
            state.currentPlayer < rules::MaxPlayers
                ? state.players[state.currentPlayer].currentSquare
                : -1;
        const auto buyAuctionPopup = buyAuctionPopup_.sync(
            inputs.desiredBuyAuctionSquare,
            inputs.desired2DView,
            ruleCurrentSquare,
            playback);
        if (!buyAuctionPopup)
            return buyAuctionPopup;

        const auto propertyHover = propertyHover_.sync(
            state, inputs.propertyTitles, inputs.propertyCurrentMouseOver,
            inputs.tick, playback);
        if (!propertyHover)
            return propertyHover;

        const auto scoreStrip = scoreStrip_.sync(inputs.scoreStrip, playback);
        if (!scoreStrip)
            return scoreStrip;

        // UDIBar.cpp shows the bank during DISPLAY_UDIBAR_Show(), before
        // DISPLAY_UDPIECES_Show() starts the dice at the same priority.
        // Bank hover follows IBarPlayerCurrentMouseOver == RULE_MAX_PLAYERS
        // and moves the existing priority-256 sequence down by one pixel.
        const auto bank = bank_.sync(visible, inputs.bankHovered, playback);
        if (!bank)
        {
            return bank;
        }

        const auto currentPlayer = currentPlayer_.sync(state, visible, playback);
        if (!currentPlayer)
            return currentPlayer;

        consumedPressedButton_ = consumedPressed;
        return {};
    }
}
