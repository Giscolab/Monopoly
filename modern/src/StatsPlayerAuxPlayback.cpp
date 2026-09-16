#include "StatsPlayerAuxPlayback.hpp"

#include "LegacyBitmap.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <variant>

namespace monopoly::statsui
{
    namespace
    {
        using Objects = std::vector<PlayerAuxPlayback::Published>;

        [[nodiscard]] constexpr bool bssmMode(ibar::RuleMode mode) noexcept
        {
            return mode == ibar::RuleMode::Build ||
                mode == ibar::RuleMode::Sell ||
                mode == ibar::RuleMode::Mortgage ||
                mode == ibar::RuleMode::UnMortgage;
        }

        [[nodiscard]] constexpr data::DataId languageId(data::DataTag tag) noexcept
        {
            return data::packDataId(data::LegacyGroupId::LanguageGraphics, tag);
        }
        struct AuxCatalog
        {
            data::DataTag jailBase{};
            data::DataTag future{};
            data::DataTag immunity{};
        };

        [[nodiscard]] constexpr AuxCatalog catalog(data::BoardEdition edition) noexcept
        {
            if (edition == data::BoardEdition::Europe)
            {
                return {PlayerJailEuropeBaseTag,
                    PlayerFutureEuropeTag, PlayerImmunityEuropeTag};
            }
            return {PlayerJailUsBaseTag,
                PlayerFutureUsTag, PlayerImmunityUsTag};
        }

        [[nodiscard]] std::expected<std::pair<int, int>, std::string>
        bitmapSize(const sequence::SequenceProgram& program)
        {
            const auto resources = program.resources();
            if (!resources)
                return std::unexpected("UDStats Player aux sequence has no resources");
            for (const auto& description : program.descriptions())
            {
                if (!std::holds_alternative<data::SequenceBitmapData>(
                        description.record.data) || !description.contentsDataId)
                    continue;

                const auto id = *description.contentsDataId;
                const auto metadata = resources->banks().metadata(id);
                const auto bytes = resources->banks().load(id);
                if (!metadata || !bytes)
                    return std::unexpected("UDStats Player aux bitmap dependency failed");
                if (metadata->type == data::LegacyDataType::Bitmap)
                {
                    const auto bitmap = data::inspectLegacyBitmap(**bytes);
                    if (!bitmap) return std::unexpected(bitmap.error().detail);
                    return std::pair{bitmap->width, bitmap->height};
                }
                if (metadata->type == data::LegacyDataType::Uap)
                {
                    const auto bitmap = data::inspectLegacyUap(**bytes);
                    if (!bitmap) return std::unexpected(bitmap.error().detail);
                    return std::pair{static_cast<int>(bitmap->width),
                        static_cast<int>(bitmap->height)};
                }
            }
            return std::unexpected("UDStats Player aux sequence contains no bitmap");
        }
        [[nodiscard]] bool hasHit(
            const rules::GameState& gameState,
            rules::PlayerNumber player,
            rules::CountHitType type) noexcept
        {
            return std::any_of(gameState.countHits.begin(), gameState.countHits.end(),
                [&](const rules::CountHitRecord& hit)
                {
                    return hit.toPlayer == player && hit.hitType == type;
                });
        }

        [[nodiscard]] std::expected<Objects, std::string> desiredObjects(
            const State& state, const rules::GameState& gameState,
            const PlayerPlaybackInputs& inputs,
            engine::SequencePlayback& playback)
        {
            Objects result;
            const auto count = std::min<std::size_t>(state.playerCount,
                std::min<std::size_t>(gameState.numberOfPlayers, rules::MaxPlayers));
            const bool localBssm = bssmMode(inputs.mode) && inputs.iBarPlayerLocalHuman;
            const int boxWidth = count > 4 ? 130 : 198;
            const int boxHeight = count > 4 ? 226 : 222;
            const auto ids = catalog(playback.resources()->context().board);
            const auto futureId = languageId(ids.future);
            const auto immunityId = languageId(ids.immunity);
            auto futureProgram = sequence::SequenceProgram::load(
                playback.resources(), futureId);
            if (!futureProgram)
                return std::unexpected("UDStats Future icon failed: " +
                    futureProgram.error().detail);
            auto immunityProgram = sequence::SequenceProgram::load(
                playback.resources(), immunityId);
            if (!immunityProgram)
                return std::unexpected("UDStats Immunity icon failed: " +
                    immunityProgram.error().detail);
            const auto futureSize = bitmapSize(**futureProgram);
            if (!futureSize) return std::unexpected(futureSize.error());
            const auto immunitySize = bitmapSize(**immunityProgram);
            if (!immunitySize) return std::unexpected(immunitySize.error());

            int iconGapOffset = 3;
            for (std::size_t column = 0; column < count; ++column)
            {
                const auto player = state.playerOrder[column];
                if (player >= gameState.numberOfPlayers || player >= rules::MaxPlayers)
                    return std::unexpected("UDStats Player aux references invalid player");
                if (localBssm && player != inputs.iBarPlayer)
                    continue;
                if (hasHit(gameState, player, rules::CountHitType::RentImmunity))
                {
                    result.push_back({immunityId, PlayerAuxPriority,
                        static_cast<int>(column) * boxWidth + boxWidth -
                            immunitySize->first - 10 + iconGapOffset,
                        224 + boxHeight - immunitySize->second});
                }
                if (hasHit(gameState, player, rules::CountHitType::FutureRent))
                {
                    result.push_back({futureId, PlayerAuxPriority,
                        static_cast<int>(column) * boxWidth + boxWidth -
                            futureSize->first - 10 + iconGapOffset,
                        224 + boxHeight - futureSize->second - 20});
                }

                const int deedGapOffset = 3 + 3 * static_cast<int>(column);
                for (std::size_t deck = 0; deck < gameState.cards.size(); ++deck)
                {
                    if (gameState.cards[deck].jailOwner != player) continue;
                    result.push_back({languageId(static_cast<data::DataTag>(
                            ids.jailBase + deck)), PlayerAuxPriority,
                        static_cast<int>(column) * boxWidth + deedGapOffset + 10,
                        385 + static_cast<int>(deck) * 30});
                }
                iconGapOffset += 3;
            }
            return result;
        }
    }
    std::expected<void, std::string> PlayerAuxPlayback::sync(
        const State& state, const rules::GameState& gameState,
        const PlayerPlaybackInputs& inputs,
        display::Screen2D desiredView,
        engine::SequencePlayback& playback)
    {
        Objects desired;
        if (desiredView == display::Screen2D::Portfolio &&
            state.screen == Screen::Player)
        {
            auto planned = desiredObjects(state, gameState, inputs, playback);
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
                    "UDStats Player aux resource failed: " + loaded.error().detail);
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
                "sequence command queue cannot fit UDStats Player aux transition");
        }

        for (const auto& [id, priority] : stops)
        {
            if (!playback.commands().enqueue(
                    sequence::StopSequenceCommand{id, priority, false}))
                return std::unexpected("validated UDStats Player aux stop rejected");
        }

        for (const auto& object : desired)
        {
            if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                    programs.at(object.id), object.priority, {},
                    sequence::moveXYTransform(object.x, object.y)}))
                return std::unexpected("validated UDStats Player aux start rejected");
        }
        current_ = std::move(desired);
        return {};
    }
}
