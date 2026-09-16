#include "StatsPlayerCashPlayback.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace monopoly::statsui
{
    namespace
    {
        [[nodiscard]] constexpr bool bssmMode(ibar::RuleMode mode) noexcept
        {
            return mode == ibar::RuleMode::Build ||
                mode == ibar::RuleMode::Sell ||
                mode == ibar::RuleMode::Mortgage ||
                mode == ibar::RuleMode::UnMortgage;
        }

        [[nodiscard]] constexpr int playerBoxWidth(
            std::size_t playerCount) noexcept
        {
            return playerCount > 4 ? 130 : 198;
        }

        [[nodiscard]] constexpr data::DataId cashIconId() noexcept
        {
            return data::packDataId(
                data::LegacyGroupId::Main, PlayerCashIconTag);
        }
        [[nodiscard]] std::expected<std::vector<PlayerCashPlayback::Published>,
            std::string> desiredCash(
            const State& state,
            const rules::GameState& gameState,
            const PlayerPlaybackInputs& inputs)
        {
            std::vector<PlayerCashPlayback::Published> result;
            const auto count = std::min<std::size_t>(
                state.playerCount,
                std::min<std::size_t>(gameState.numberOfPlayers,
                    rules::MaxPlayers));
            const int boxWidth = playerBoxWidth(count);
            const bool localBssm = bssmMode(inputs.mode) &&
                inputs.iBarPlayerLocalHuman;

            int boxGapOffset = 3;
            for (std::size_t column = 0; column < count; ++column)
            {
                const auto player = state.playerOrder[column];
                if (player >= gameState.numberOfPlayers ||
                    player >= rules::MaxPlayers)
                {
                    return std::unexpected(
                        "UDStats Player cash references invalid player");
                }
                if (localBssm && player != inputs.iBarPlayer)
                    continue;
                result.push_back({
                    static_cast<int>(column) * boxWidth + boxGapOffset +
                        PlayerCashLocalX,
                    224 + PlayerCashLocalY});
                boxGapOffset += 3;
            }
            return result;
        }
    }

    std::expected<void, std::string> PlayerCashPlayback::sync(
        const State& state,
        const rules::GameState& gameState,
        const PlayerPlaybackInputs& inputs,
        display::Screen2D desiredView,
        engine::SequencePlayback& playback)
    {
        std::vector<Published> desired;
        if (desiredView == display::Screen2D::Portfolio &&
            state.screen == Screen::Player)
        {
            auto planned = desiredCash(state, gameState, inputs);
            if (!planned) return std::unexpected(planned.error());
            desired = std::move(*planned);
        }

        if (desired == current_) return {};
        std::shared_ptr<const sequence::SequenceProgram> program;
        if (!desired.empty())
        {
            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), cashIconId());
            if (!loaded)
            {
                return std::unexpected(
                    "UDStats Player cash icon failed: " + loaded.error().detail);
            }
            program = std::move(*loaded);
        }

        const std::size_t required =
            (current_.empty() ? 0U : 1U) + desired.size();
        if (required > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit UDStats Player cash transition");
        }

        if (!current_.empty() &&
            !playback.commands().enqueue(sequence::StopSequenceCommand{
                cashIconId(), PlayerCashPriority, false}))
        {
            return std::unexpected(
                "validated UDStats Player cash stop rejected");
        }
        for (const auto& object : desired)
        {
            if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                    program, PlayerCashPriority, {},
                    sequence::moveXYTransform(object.x, object.y)}))
            {
                return std::unexpected(
                    "validated UDStats Player cash start rejected");
            }
        }

        current_ = std::move(desired);
        return {};
    }
}
