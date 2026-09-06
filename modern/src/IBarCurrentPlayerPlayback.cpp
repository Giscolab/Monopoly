#include "IBarCurrentPlayerPlayback.hpp"

#include <utility>
#include <variant>
#include <vector>

namespace monopoly::ibar
{
    std::expected<data::DataId, std::string>
        desiredCurrentPlayerToken(
            const rules::GameState& state,
            bool visible)
    {
        if (!visible ||
            state.currentPlayer >= state.numberOfPlayers ||
            state.currentPlayer >= rules::MaxPlayers)
        {
            return data::EmptyDataId;
        }

        const auto token = state.players[state.currentPlayer].token;
        if (token >= rules::MaxTokens)
        {
            return std::unexpected(
                "current IBar player token is outside the legacy token range");
        }

        return data::packDataId(
            data::LegacyGroupId::Main,
            static_cast<data::DataTag>(
                CurrentPlayerTokenBaseTag + token));
    }

    std::expected<void, std::string> CurrentPlayerPlayback::sync(
        const rules::GameState& state,
        bool visible,
        engine::SequencePlayback& playback)
    {
        const auto resolved = desiredCurrentPlayerToken(state, visible);
        if (!resolved)
        {
            return std::unexpected(resolved.error());
        }

        const data::DataId desired = *resolved;
        if (desired == currentToken_)
        {
            return {};
        }

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
        if (currentToken_ != data::EmptyDataId)
        {
            commands.push_back(sequence::StopSequenceCommand{
                currentToken_, CurrentPlayerTokenPriority, false});
        }

        if (desired != data::EmptyDataId)
        {
            commands.push_back(sequence::StartSequenceCommand{
                program, CurrentPlayerTokenPriority, {}});
            commands.push_back(sequence::makeMoveXY(
                desired, CurrentPlayerTokenPriority,
                CurrentPlayerTokenX, CurrentPlayerTokenY));
            commands.push_back(sequence::SetSequenceEndingActionCommand{
                desired, CurrentPlayerTokenPriority,
                LoopToBeginning, false});
        }

        if (commands.size() >
            sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit current IBar player transition");
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
                    "validated current IBar player command rejected");
            }
        }

        currentToken_ = desired;
        return {};
    }
}
