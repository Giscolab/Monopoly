#include "IBarBackdropPlayback.hpp"
#include "RuntimeState.hpp"

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
        const bool actionButtonsStable =
            stable(cameraButton_.visualState()) &&
            stable(mainButton_.visualState()) &&
            stable(optionsButton_.visualState()) &&
            stable(rollDiceButton_.visualState()) &&
            stable(statusButton_.visualState()) &&
            stable(tradeButton_.visualState());

        // UDIBar.cpp processes the action-button bar before the score/bank
        // section. Camera is always desired Idle while the IBar is visible.
        const auto camera = cameraButton_.sync(
            visible, playback, false, actionButtonsStable);
        if (!camera)
        {
            return camera;
        }

        const bool gameInProgress = runtime::state().gameInProgress;

        const auto mainButton = mainButton_.sync(
            visible && gameInProgress &&
                (inputs.desired2DView == display::Screen2D::Portfolio ||
                 inputs.desired2DView == display::Screen2D::Trade),
            playback, false, actionButtonsStable);
        if (!mainButton)
        {
            return mainButton;
        }

        const auto options = optionsButton_.sync(
            visible && gameInProgress,
            playback, actionButtonsStable);
        if (!options)
        {
            return options;
        }

        const auto rollDiceButton = rollDiceButton_.sync(
            visible && inputs.rollDiceDesired,
            playback,
            inputs.aiButtonRemoteState,
            actionButtonsStable);
        if (!rollDiceButton)
        {
            return rollDiceButton;
        }

        const auto statusButton = statusButton_.sync(
            visible && gameInProgress &&
                inputs.desired2DView == display::Screen2D::Main,
            playback, false, actionButtonsStable);
        if (!statusButton)
        {
            return statusButton;
        }

        const auto tradeButton = tradeButton_.sync(
            visible && gameInProgress && inputs.tradeEligible &&
                inputs.desired2DView != display::Screen2D::Trade,
            playback, false, actionButtonsStable);
        if (!tradeButton)
        {
            return tradeButton;
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
