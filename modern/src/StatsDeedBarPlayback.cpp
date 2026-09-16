#include "StatsDeedBarPlayback.hpp"

#include <map>
#include <memory>
#include <set>
#include <utility>

namespace monopoly::statsui
{
    namespace
    {
        using Objects = std::vector<DeedBarPlayback::Published>;

        [[nodiscard]] constexpr data::DataId mainId(data::DataTag tag) noexcept
        {
            return data::packDataId(data::LegacyGroupId::Main, tag);
        }

        [[nodiscard]] std::expected<Objects, std::string> desiredObjects(
            const State& state, const rules::GameState& gameState,
            const PlayerPlaybackInputs& inputs)
        {
            Objects result;
            if (state.activeSort != 1) return result;

            auto grid = planDeedGrid(state, gameState, inputs);
            if (!grid) return std::unexpected(grid.error());
            for (const auto& item : *grid)
            {
                const auto owner = gameState.squares[
                    static_cast<std::size_t>(item.square)].owner;
                if (owner >= gameState.numberOfPlayers ||
                    owner >= rules::MaxPlayers)
                {
                    continue;
                }
                const auto colour = gameState.players[owner].colour;
                if (colour >= rules::MaxPlayerColours)
                    return std::unexpected("UDStats Deed owner colour is out of range");

                result.push_back({mainId(static_cast<data::DataTag>(
                        DeedOwnerBarBaseTag + colour)),
                    DeedOwnerBarPriority,
                    item.x + DeedOwnerBarOffsetX,
                    item.y + DeedOwnerBarOffsetY});
            }
            return result;
        }
    }
    std::expected<void, std::string> DeedBarPlayback::sync(
        const State& state, const rules::GameState& gameState,
        const PlayerPlaybackInputs& inputs,
        display::Screen2D desiredView,
        engine::SequencePlayback& playback)
    {
        Objects desired;
        if (desiredView == display::Screen2D::Portfolio &&
            state.screen == Screen::Deed)
        {
            auto planned = desiredObjects(state, gameState, inputs);
            if (!planned) return std::unexpected(planned.error());
            desired = std::move(*planned);
        }

        if (desired == current_) return {};

        std::map<data::DataId,
            std::shared_ptr<const sequence::SequenceProgram>> programs;
        for (const auto& object : desired)
        {
            if (programs.contains(object.id)) continue;
            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), object.id);
            if (!loaded)
                return std::unexpected(
                    "UDStats Deed owner bar resource failed: " + loaded.error().detail);
            programs.emplace(object.id, std::move(*loaded));
        }

        std::set<std::pair<data::DataId, std::uint16_t>> stops;
        for (const auto& object : current_)
            stops.emplace(object.id, object.priority);

        const std::size_t required = stops.size() + desired.size();
        if (required > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit UDStats Deed owner bars");
        }
        for (const auto& [id, priority] : stops)
        {
            if (!playback.commands().enqueue(
                    sequence::StopSequenceCommand{id, priority, false}))
                return std::unexpected("validated UDStats Deed owner-bar stop rejected");
        }

        for (const auto& object : desired)
        {
            if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                    programs.at(object.id), object.priority, {},
                    sequence::moveXYTransform(object.x, object.y)}))
                return std::unexpected("validated UDStats Deed owner-bar start rejected");
        }

        current_ = std::move(desired);
        return {};
    }
}
