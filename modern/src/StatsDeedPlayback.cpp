#include "StatsDeedPlayback.hpp"

#include <map>
#include <memory>
#include <set>
#include <utility>

namespace monopoly::statsui
{
    namespace
    {
        using Objects = std::vector<DeedPlayback::Published>;

        [[nodiscard]] constexpr bool bssmMode(ibar::RuleMode mode) noexcept
        {
            return mode == ibar::RuleMode::Build ||
                mode == ibar::RuleMode::Sell ||
                mode == ibar::RuleMode::Mortgage ||
                mode == ibar::RuleMode::UnMortgage;
        }

        [[nodiscard]] data::DataId deedId(int square, bool mortgaged) noexcept
        {
            const int property = ibar::layout::propertyIndex(square);
            if (property < 0) return data::EmptyDataId;
            const auto base = mortgaged
                ? PlayerDeedMortgagedBaseTag : PlayerDeedNormalBaseTag;
            return data::packDataId(data::LegacyGroupId::Patterns,
                static_cast<data::DataTag>(base + property));
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

        [[nodiscard]] bool useMortgagedFace(
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
            const bool localBssm = bssmMode(inputs.mode) &&
                inputs.iBarPlayerLocalHuman;
            int displayed = 0;

            for (std::size_t rank = 0; rank < rules::SquareCount; ++rank)
            {
                const int square = state.deedOrder[rank];
                if (square < 0 || square >= static_cast<int>(rules::SquareCount))
                    return std::unexpected("UDStats Deed sort references invalid square");
                if (ibar::layout::propertyIndex(square) < 0) continue;
                if (state.activeSort == 3 && state.deedMetric[rank] == 0) continue;

                const auto owner = gameState.squares[static_cast<std::size_t>(square)].owner;
                if (localBssm)
                {
                    if (owner != inputs.iBarPlayer) continue;
                    if (!deedVisible(gameState, square, inputs)) continue;
                }

                const auto id = deedId(square,
                    useMortgagedFace(gameState, square, inputs.mode));
                if (id == data::EmptyDataId)
                    return std::unexpected("UDStats Deed square has no deed sequence");
                const int x = DeedGridX + DeedGridColumnStep *
                    (displayed % DeedGridColumns);
                const int y = DeedGridY + DeedGridRowStep *
                    (displayed / DeedGridColumns);
                result.push_back({square, id, DeedGridPriority, x, y});
                ++displayed;
            }
            return result;
        }
    }

    std::expected<std::vector<DeedPlayback::Published>, std::string>
    planDeedGrid(const State& state, const rules::GameState& gameState,
        const PlayerPlaybackInputs& inputs)
    {
        return desiredObjects(state, gameState, inputs);
    }

    std::expected<void, std::string> DeedPlayback::sync(
        const State& state, const rules::GameState& gameState,
        const PlayerPlaybackInputs& inputs,
        display::Screen2D desiredView, engine::SequencePlayback& playback)
    {
        Objects desired;
        if (desiredView == display::Screen2D::Portfolio &&
            state.screen == Screen::Deed)
        {
            auto planned = planDeedGrid(state, gameState, inputs);
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
                    "UDStats Deed resource failed: " + loaded.error().detail);
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
                "sequence command queue cannot fit UDStats Deed transition");
        }
        for (const auto& [id, priority] : stops)
        {
            if (!playback.commands().enqueue(
                    sequence::StopSequenceCommand{id, priority, false}))
                return std::unexpected("validated UDStats Deed stop rejected");
        }

        for (const auto& object : desired)
        {
            if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                    programs.at(object.id), object.priority, {},
                    sequence::moveXYTransform(object.x, object.y)}))
                return std::unexpected("validated UDStats Deed start rejected");
        }

        current_ = std::move(desired);
        return {};
    }
}
