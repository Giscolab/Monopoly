#include "StatsPlayerPlayback.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <set>
#include <utility>

namespace monopoly::statsui
{
    namespace
    {
        using Objects = std::vector<PlayerPlayback::Published>;

        [[nodiscard]] constexpr bool bssmMode(ibar::RuleMode mode) noexcept
        {
            return mode == ibar::RuleMode::Build ||
                mode == ibar::RuleMode::Sell ||
                mode == ibar::RuleMode::Mortgage ||
                mode == ibar::RuleMode::UnMortgage;
        }

        [[nodiscard]] constexpr data::DataId mainId(data::DataTag tag) noexcept
        {
            return data::packDataId(data::LegacyGroupId::Main, tag);
        }

        [[nodiscard]] data::DataId deedId(
            int square, bool mortgaged) noexcept
        {
            const int property = ibar::layout::propertyIndex(square);
            if (property < 0) return data::EmptyDataId;
            const auto base = mortgaged
                ? PlayerDeedMortgagedBaseTag : PlayerDeedNormalBaseTag;
            return data::packDataId(data::LegacyGroupId::Patterns,
                static_cast<data::DataTag>(base + property));
        }

        struct PlayerGeometry
        {
            int boxWidth{};
            int boxHeight{};
            int deedBoxWidth{};
            int deedBoxX{};
            int deedBoxY{};
            data::DataTag boxBaseTag{};
        };

        [[nodiscard]] constexpr PlayerGeometry playerGeometry(
            std::size_t playerCount) noexcept
        {
            if (playerCount > 4)
                return {130, 226, 120, 5, 300, PlayerBoxSmallBaseTag};
            return {198, 222, 130, 61, 275, PlayerBoxLargeBaseTag};
        }

        [[nodiscard]] bool deedVisible(
            const rules::GameState& gameState, int square,
            const PlayerPlaybackInputs& inputs) noexcept
        {
            const auto bit = ibar::layout::propertyBit(square);
            switch (inputs.mode)
            {
            case ibar::RuleMode::Mortgage:
                return bit != 0 && (inputs.mortgageProperties & bit) != 0;
            case ibar::RuleMode::UnMortgage:
                return gameState.squares[static_cast<std::size_t>(square)].mortgaged;
            case ibar::RuleMode::Build:
                return bit != 0 && (inputs.buildProperties & bit) != 0;
            case ibar::RuleMode::Sell:
                return bit != 0 && (inputs.sellProperties & bit) != 0;
            default:
                return true;
            }
        }

        [[nodiscard]] bool deedUsesMortgagedFace(
            const rules::GameState& gameState, int square,
            ibar::RuleMode mode) noexcept
        {
            if (mode == ibar::RuleMode::Mortgage ||
                mode == ibar::RuleMode::Build || mode == ibar::RuleMode::Sell)
                return false;
            return gameState.squares[static_cast<std::size_t>(square)].mortgaged;
        }

        [[nodiscard]] std::expected<Objects, std::string> desiredObjects(
            const State& state, const rules::GameState& gameState,
            const PlayerPlaybackInputs& inputs)
        {
            Objects result;
            const auto count = std::min<std::size_t>(
                state.playerCount, std::min<std::size_t>(
                    gameState.numberOfPlayers, rules::MaxPlayers));
            const auto geometry = playerGeometry(count);
            const bool localBssm = bssmMode(inputs.mode) &&
                inputs.iBarPlayerLocalHuman;

            int boxGapOffset = 3;
            for (std::size_t column = 0; column < count; ++column)
            {
                const auto player = state.playerOrder[column];
                if (player >= gameState.numberOfPlayers || player >= rules::MaxPlayers)
                    return std::unexpected(
                        "UDStats Player sort references invalid player");
                if (localBssm && player != inputs.iBarPlayer)
                    continue;

                const auto colour = gameState.players[player].colour;
                if (colour >= rules::MaxPlayerColours)
                    return std::unexpected(
                        "UDStats Player colour is out of range");
                const int deedColumnOffset = 3 + 3 * static_cast<int>(column);
                result.push_back({
                    mainId(static_cast<data::DataTag>(geometry.boxBaseTag + colour)),
                    PlayerBoxPriority,
                    static_cast<int>(column) * geometry.boxWidth + boxGapOffset,
                    224});
                boxGapOffset += 3;

                std::array<bool, 11> counted{};
                for (int square = 0; square < static_cast<int>(rules::SquareCount); ++square)
                {
                    if (gameState.squares[static_cast<std::size_t>(square)].owner != player)
                        continue;
                    const int order = ibar::layout::propertyBarOrder(square);
                    if (order < 0) continue;
                    counted[static_cast<std::size_t>(order / 3)] = true;
                }

                std::array<int, 11> compressed{};
                compressed.fill(-1);
                int numberOfColumns = 0;
                for (std::size_t index = 0; index < counted.size(); ++index)
                    if (counted[index]) compressed[index] = numberOfColumns++;

                int widthApart = 0;
                if (numberOfColumns != 0)
                    widthApart = (geometry.deedBoxWidth - 8 - PlayerDeedWidth) /
                        numberOfColumns;
                widthApart = std::min(widthApart, PlayerDeedWidth * 2);

                for (int square = 0; square < static_cast<int>(rules::SquareCount); ++square)
                {
                    if (gameState.squares[static_cast<std::size_t>(square)].owner != player)
                        continue;
                    const int order = ibar::layout::propertyBarOrder(square);
                    if (order < 0 || !deedVisible(gameState, square, inputs))
                        continue;
                    const int groupColumn = compressed[static_cast<std::size_t>(order / 3)];
                    if (groupColumn < 0)
                        return std::unexpected(
                            "UDStats Player deed column compression failed");
                    const int depth = order % 3;
                    const int x = static_cast<int>(column) * geometry.boxWidth +
                        deedColumnOffset + geometry.deedBoxX +
                        groupColumn * widthApart + 4 * depth;
                    const int y = geometry.deedBoxY + 20 * depth;
                    const auto id = deedId(square,
                        deedUsesMortgagedFace(gameState, square, inputs.mode));
                    if (id == data::EmptyDataId)
                        return std::unexpected(
                            "UDStats Player owned square has no deed sequence");
                    result.push_back({id,
                        static_cast<std::uint16_t>(PlayerDeedBasePriority + order),
                        x, y});
                }
            }
            return result;
        }
    }

    std::expected<void, std::string> PlayerPlayback::sync(
        const State& state, const rules::GameState& gameState,
        const PlayerPlaybackInputs& inputs,
        display::Screen2D desiredView, engine::SequencePlayback& playback)
    {
        Objects desired;
        if (desiredView == display::Screen2D::Portfolio &&
            state.screen == Screen::Player)
        {
            auto planned = desiredObjects(state, gameState, inputs);
            if (!planned) return std::unexpected(planned.error());
            desired = std::move(*planned);
        }

        if (desired == current_) return {};

        std::map<data::DataId, std::shared_ptr<const sequence::SequenceProgram>> programs;
        for (const auto& object : desired)
        {
            if (programs.contains(object.id)) continue;
            auto loaded = sequence::SequenceProgram::load(playback.resources(), object.id);
            if (!loaded)
                return std::unexpected(
                    "UDStats Player overlay resource failed: " + loaded.error().detail);
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
                "sequence command queue cannot fit UDStats Player overlay transition");
        }

        for (const auto& [id, priority] : stops)
        {
            if (!playback.commands().enqueue(
                    sequence::StopSequenceCommand{id, priority, false}))
                return std::unexpected("validated UDStats Player stop rejected");
        }
        for (const auto& object : desired)
        {
            if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                    programs.at(object.id), object.priority, {},
                    sequence::moveXYTransform(object.x, object.y)}))
                return std::unexpected("validated UDStats Player start rejected");
        }

        current_ = std::move(desired);
        return {};
    }
}
