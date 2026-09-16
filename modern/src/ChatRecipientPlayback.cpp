#include "ChatRecipientPlayback.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <utility>

namespace monopoly::chat
{
    namespace
    {
        [[nodiscard]] constexpr data::DataId mainId(data::DataTag tag) noexcept
        {
            return data::packDataId(data::LegacyGroupId::Main, tag);
        }

        using Objects = std::vector<RecipientPlayback::Published>;

        [[nodiscard]] Objects desiredObjects(
            const State& state,
            const rules::GameState& gameState)
        {
            Objects result;
            if (!state.boxActive) return result;

            result.push_back({mainId(ChatAllTag), ChatBarPriority, 183, 12});
            std::size_t ordinal{};
            const auto playerCount = std::min<std::size_t>(
                gameState.numberOfPlayers, rules::MaxPlayers);
            for (int player = static_cast<int>(playerCount) - 1;
                 player >= 0; --player)
            {
                const auto bit = 1u << static_cast<unsigned>(player);
                if ((state.eligibleRecipients & bit) == 0u) continue;

                const auto colour = gameState.players[static_cast<std::size_t>(player)].colour;
                if (colour >= rules::MaxPlayerColours) continue;

                const int baseX = 164 - 19 * static_cast<int>(ordinal);
                result.push_back({mainId(static_cast<data::DataTag>(
                    ChatPlayerColourBaseTag + colour)), ChatBarPriority, baseX, 13});
                if ((state.recipientMask & bit) != 0u)
                {
                    result.push_back({mainId(static_cast<data::DataTag>(
                        ChatPlayerFocusBaseTag + colour)), ChatBarPriority,
                        baseX + 4, 15});
                }
                ++ordinal;
            }
            return result;
        }
    }
    std::expected<void, std::string> RecipientPlayback::sync(
        const State& state,
        const rules::GameState& gameState,
        engine::SequencePlayback& playback)
    {
        auto desired = desiredObjects(state, gameState);
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
                    "UDChat recipient resource failed: " + loaded.error().detail);
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
                "sequence command queue cannot fit UDChat recipient transition");
        }

        for (const auto& [id, priority] : stops)
        {
            if (!playback.commands().enqueue(
                    sequence::StopSequenceCommand{id, priority, false}))
                return std::unexpected("validated UDChat recipient stop rejected");
        }
        for (const auto& object : desired)
        {
            if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                    programs.at(object.id), object.priority, {},
                    sequence::moveXYTransform(object.x, object.y)}))
                return std::unexpected("validated UDChat recipient start rejected");
        }

        current_ = std::move(desired);
        return {};
    }
}
