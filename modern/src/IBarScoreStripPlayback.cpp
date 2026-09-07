#include "IBarScoreStripPlayback.hpp"

#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::ibar
{
    namespace
    {
        [[nodiscard]] data::DataId mainData(data::DataTag tag) noexcept
        {
            return data::packDataId(data::LegacyGroupId::Main, tag);
        }

        [[nodiscard]] std::uint16_t colourPriority(std::size_t player) noexcept
        {
            return static_cast<std::uint16_t>(ScoreGeneralPriority + 1 + player);
        }

        [[nodiscard]] std::uint16_t tokenPriority(std::size_t player) noexcept
        {
            return static_cast<std::uint16_t>(ScoreBoxPriority + player);
        }

        [[nodiscard]] std::uint16_t jailPriority(std::size_t player) noexcept
        {
            return static_cast<std::uint16_t>(ScoreBoxPriority + 1 + player);
        }
        [[nodiscard]] std::expected<std::shared_ptr<const sequence::SequenceProgram>, std::string>
        loadProgram(engine::SequencePlayback& playback, data::DataId id)
        {
            auto loaded = sequence::SequenceProgram::load(playback.resources(), id);
            if (!loaded)
                return std::unexpected(loaded.error().detail);
            return std::move(*loaded);
        }

        void addStartXY(
            std::vector<sequence::SequenceCommand>& commands,
            std::shared_ptr<const sequence::SequenceProgram> program,
            data::DataId id,
            std::uint16_t priority,
            int x,
            int y)
        {
            commands.push_back(sequence::StartSequenceCommand{
                std::move(program), priority, {}});
            commands.push_back(sequence::makeMoveXY(id, priority, x, y));
        }
    }

    std::expected<ScoreStripPlan, std::string> planScoreStrip(
        const rules::GameState& state,
        const ScoreStripInputs& inputs)
    {
        if (state.numberOfPlayers > rules::MaxPlayers)
            return std::unexpected("IBar score strip player count exceeds legacy maximum");
        ScoreStripPlan result{};
        result.tick = inputs.tick;
        const int count = static_cast<int>(state.numberOfPlayers);
        if (count == 0) return result;

        const int width = layout::scoreBoxWidth(count);
        const data::DataTag colourBase = count <= 4
            ? ScoreLargeColourBaseTag : ScoreSmallColourBaseTag;

        for (int player = 0; player < count; ++player)
        {
            if (!inputs.visiblePlayers[static_cast<std::size_t>(player)])
                continue;

            const auto& source = state.players[static_cast<std::size_t>(player)];
            if (source.token >= rules::MaxTokens)
                return std::unexpected("IBar score token is outside legacy token range");
            if (source.colour >= ScorePlayerColourCount)
                return std::unexpected("IBar score colour is outside legacy 0..5 range");

            auto& target = result.players[static_cast<std::size_t>(player)];
            target.visible = true;
            target.token = mainData(static_cast<data::DataTag>(ScoreTokenBaseTag + source.token));
            target.colourBar = mainData(static_cast<data::DataTag>(colourBase + source.colour));
            target.jailBars = inputs.gameInProgress && source.currentSquare == 40;
            target.x = layout::scoreX(player, count, width);
            target.width = width;
            target.hovered = inputs.hoveredPlayer == player;
            target.cash = source.cash;
            target.name = source.name;
        }
        return result;
    }

    std::expected<void, std::string> ScoreStripPlayback::sync(
        const ScoreStripPlan& plan,
        engine::SequencePlayback& playback)
    {
        for (std::size_t player = 0; player < players_.size(); ++player)
        {
            const auto& desired = plan.players[player];
            auto current = players_[player];
            const auto& text = text_[player];
            auto nextText = text;
            nextText.redrawRequested = false;
            nextText.lastCashChange = ScoreCashChange::None;

            if (!desired.visible)
            {
                if (!current.visible)
                {
                    text_[player] = std::move(nextText);
                    continue;
                }
                std::vector<sequence::SequenceCommand> commands;
                if (current.colourBar != data::EmptyDataId)
                    commands.push_back(sequence::StopSequenceCommand{
                        current.colourBar, colourPriority(player), false});
                if (current.token != data::EmptyDataId)
                    commands.push_back(sequence::StopSequenceCommand{
                        current.token, tokenPriority(player), false});
                if (current.jailBars)
                    commands.push_back(sequence::StopSequenceCommand{
                        mainData(ScoreJailBarsTag), jailPriority(player), false});

                if (commands.size() > sequence::SequenceCommandQueue::Capacity -
                        playback.commands().pendingCount())
                    return std::unexpected("sequence command queue cannot fit IBar score hide transition");

                for (auto& command : commands)
                {
                    const auto queued = std::visit(
                        [&](auto value)
                        {
                            return playback.commands().enqueue(std::move(value));
                        },
                        std::move(command));
                    if (!queued)
                        return std::unexpected("validated IBar score hide command rejected");
                }
                players_[player] = {};
                text_[player] = std::move(nextText);
                continue;
            }

            const bool changed = !current.visible ||
                current.token != desired.token ||
                current.colourBar != desired.colourBar ||
                current.jailBars != desired.jailBars ||
                current.x != desired.x ||
                current.hovered != desired.hovered;
            const bool textChanged = desired.cash != text.displayedCash ||
                desired.name != text.printedName;
            if (changed || textChanged)
            {
                if (desired.cash != text.displayedCash &&
                    text.lastCashUpdateTick <= plan.tick - 20u)
                {
                    nextText.lastCashChange = desired.cash > text.displayedCash
                        ? ScoreCashChange::Up : ScoreCashChange::Down;
                    nextText.displayedCash = desired.cash;
                    nextText.lastCashUpdateTick = plan.tick;
                }
                nextText.printedName = desired.name;
                nextText.redrawRequested = true;
            }
            if (!changed)
            {
                text_[player] = std::move(nextText);
                continue;
            }
            const auto tokenProgram = loadProgram(playback, desired.token);
            if (!tokenProgram) return std::unexpected(tokenProgram.error());
            const auto colourProgram = loadProgram(playback, desired.colourBar);
            if (!colourProgram) return std::unexpected(colourProgram.error());

            std::shared_ptr<const sequence::SequenceProgram> jailProgram;
            if (desired.jailBars)
            {
                const auto loadedJail = loadProgram(playback, mainData(ScoreJailBarsTag));
                if (!loadedJail) return std::unexpected(loadedJail.error());
                jailProgram = *loadedJail;
            }

            std::vector<sequence::SequenceCommand> commands;
            if (current.colourBar != data::EmptyDataId)
                commands.push_back(sequence::StopSequenceCommand{
                    current.colourBar, colourPriority(player), false});
            if (current.token != data::EmptyDataId)
                commands.push_back(sequence::StopSequenceCommand{
                    current.token, tokenPriority(player), false});

            const int hoverOffset = desired.hovered ? 1 : 0;
            addStartXY(commands, *tokenProgram, desired.token,
                tokenPriority(player), desired.x + 5,
                layout::ScoreY + 5 + hoverOffset);
            addStartXY(commands, *colourProgram, desired.colourBar,
                colourPriority(player), desired.x,
                layout::ScoreY + hoverOffset);
            commands.push_back(sequence::StopSequenceCommand{
                mainData(ScoreJailBarsTag), jailPriority(player), false});
            if (desired.jailBars)
            {
                addStartXY(commands, jailProgram, mainData(ScoreJailBarsTag),
                    jailPriority(player), desired.x + 1,
                    layout::ScoreY - 1 + hoverOffset);
            }

            if (commands.size() > sequence::SequenceCommandQueue::Capacity -
                    playback.commands().pendingCount())
                return std::unexpected("sequence command queue cannot fit IBar score transition");

            for (auto& command : commands)
            {
                const auto queued = std::visit(
                    [&](auto value)
                    {
                        return playback.commands().enqueue(std::move(value));
                    },
                    std::move(command));
                if (!queued)
                    return std::unexpected("validated IBar score command rejected");
            }

            players_[player] = ScoreStripPlayerRuntime{
                true, desired.token, desired.colourBar,
                desired.jailBars, desired.x, desired.hovered};
            text_[player] = std::move(nextText);
        }
        return {};
    }
}
