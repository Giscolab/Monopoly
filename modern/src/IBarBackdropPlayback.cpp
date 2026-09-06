#include "IBarBackdropPlayback.hpp"

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
        engine::SequencePlayback& playback)
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

        return currentPlayer_.sync(state, visible, playback);
    }
}
