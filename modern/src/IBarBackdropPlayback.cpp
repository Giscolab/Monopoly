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

    std::expected<void, std::string> BackdropPlayback::sync(
        const rules::GameState& state,
        bool visible,
        rules::PlayerNumber activePlayer,
        engine::SequencePlayback& playback,
        ActionButtonInputs inputs)
    {
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
        const bool gameOver = inputs.ruleMode == RuleMode::GameOver;

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
            ButtonRequest{&flatTaxButton_, visible && taxDecision, inputs.aiButtonRemoteState},
            ButtonRequest{&percentageButton_, visible && taxDecision, inputs.aiButtonRemoteState},
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
            ButtonRequest{&exitButton_, visible && gameOver, false},
            ButtonRequest{&useCardButton_, visible && wantsUseCard(inputs.ruleMode),
                inputs.aiButtonRemoteState}
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
        for (const auto& request : requests)
        {
            const auto result = request.button->sync(
                request.desired, playback, request.useGrey,
                actionButtonsStable, !justChanged);
            if (!result)
                return result;
        }

        // UDIBar.cpp shows the bank during DISPLAY_UDIBAR_Show(), before
        // DISPLAY_UDPIECES_Show() starts the dice at the same priority.
        // Main-screen bank hover tracking is not wired yet, so engine
        // integration deliberately starts from the non-hovered source state.
        const auto bank = bank_.sync(visible, false, playback);
        if (!bank)
        {
            return bank;
        }

        return currentPlayer_.sync(state, visible, playback);
    }
}
