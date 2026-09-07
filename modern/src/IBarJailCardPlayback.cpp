#include "IBarJailCardPlayback.hpp"

#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::ibar
{
    std::expected<void, std::string> JailCardPlayback::sync(
        const rules::GameState& state,
        bool propertyBarAvailable,
        rules::PlayerNumber activePlayer,
        engine::SequencePlayback& playback)
    {
        std::array<data::DataId, 2> desired{};
        if (propertyBarAvailable && activePlayer < rules::MaxPlayers)
        {
            for (std::size_t deck = 0; deck < desired.size(); ++deck)
            {
                if (state.cards[deck].jailOwner == activePlayer)
                    desired[deck] = jailCardSequence(deck);
            }
        }

        if (desired == current_)
            return {};

        std::array<std::shared_ptr<const sequence::SequenceProgram>, 2> programs{};
        for (std::size_t deck = 0; deck < desired.size(); ++deck)
        {
            if (desired[deck] == data::EmptyDataId || desired[deck] == current_[deck])
                continue;
            auto loaded = sequence::SequenceProgram::load(playback.resources(), desired[deck]);
            if (!loaded)
                return std::unexpected(loaded.error().detail);
            programs[deck] = std::move(*loaded);
        }

        std::vector<sequence::SequenceCommand> commands;
        for (std::size_t deck = 0; deck < desired.size(); ++deck)
        {
            if (desired[deck] == current_[deck])
                continue;
            const auto priority = static_cast<std::uint16_t>(JailCardBasePriority + deck);
            if (current_[deck] != data::EmptyDataId)
                commands.push_back(sequence::StopSequenceCommand{current_[deck], priority, false});
            if (desired[deck] != data::EmptyDataId)
            {
                commands.push_back(sequence::StartSequenceCommand{programs[deck], priority, {}});
                commands.push_back(sequence::makeMoveXY(
                    desired[deck], priority, JailCardX[deck], JailCardY[deck]));
            }
        }

        if (commands.size() > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
            return std::unexpected("sequence command queue cannot fit IBar jail-card transition");

        for (auto& command : commands)
        {
            const auto queued = std::visit(
                [&](auto value) { return playback.commands().enqueue(std::move(value)); },
                std::move(command));
            if (!queued)
                return std::unexpected("validated IBar jail-card command rejected");
        }

        current_ = desired;
        return {};
    }
}
