#include "TradeTokenPlayback.hpp"

#include "LegacyBitmap.hpp"
#include "LegacySequence.hpp"
#include "SequenceRuntime.hpp"

#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::tradeui
{
    namespace
    {
        struct TokenSpec
        {
            data::DataId id{data::EmptyDataId};
            std::uint16_t priority{};
            std::int32_t x{};
            std::int32_t y{};
        };

        [[nodiscard]] std::expected<std::uint32_t, std::string>
        anyBitmapWidth(const sequence::SequenceProgram& program)
        {
            const auto resources = program.resources();
            if (!resources)
                return std::unexpected("Trade token sequence has no resource snapshot");

            for (const auto& description : program.descriptions())
            {
                if (!std::holds_alternative<data::SequenceBitmapData>(
                        description.record.data) ||
                    !description.contentsDataId)
                    continue;

                const auto id = *description.contentsDataId;
                const auto metadata = resources->banks().metadata(id);
                if (!metadata)
                    return std::unexpected(metadata.error().detail);
                const auto bytes = resources->banks().load(id);
                if (!bytes)
                    return std::unexpected(bytes.error().detail);

                if (metadata->type == data::LegacyDataType::Bitmap)
                {
                    const auto bitmap = data::inspectLegacyBitmap(**bytes);
                    if (!bitmap)
                        return std::unexpected(bitmap.error().detail);
                    if (bitmap->width <= 0)
                        return std::unexpected("Trade token bitmap width is invalid");
                    return static_cast<std::uint32_t>(bitmap->width);
                }

                if (metadata->type == data::LegacyDataType::Uap)
                {
                    const auto bitmap = data::inspectLegacyUap(**bytes);
                    if (!bitmap)
                        return std::unexpected(bitmap.error().detail);
                    if (bitmap->width == 0)
                        return std::unexpected("Trade token UAP width is invalid");
                    return bitmap->width;
                }

                return std::unexpected(
                    "Trade token 2D sequence content is not a bitmap");
            }

            return std::unexpected(
                "Trade token sequence contains no bitmap record");
        }

        [[nodiscard]] std::expected<std::array<TokenSpec, 2>, std::string> desiredTokens(
            const State& state,
            const rules::GameState& gameState,
            display::Screen2D desiredView)
        {
            std::array<TokenSpec, 2> specs{{
                {data::EmptyDataId, TradeTokenAPriority, TradeTokenAX, TradeTokenY},
                {data::EmptyDataId, TradeTokenBPriority, 0, TradeTokenY}
            }};

            if (desiredView != display::Screen2D::Trade)
                return specs;

            const std::array players{state.playerA, state.playerB};
            for (std::size_t index = 0; index < players.size(); ++index)
            {
                const auto player = players[index];
                if (player >= rules::MaxPlayers)
                    continue;
                const auto token = gameState.players[player].token;
                if (token >= rules::MaxTokens)
                    return std::unexpected("Trade token index is out of range");
                specs[index].id = tradeTokenSequence(token);
            }
            return specs;
        }
    }

    std::expected<void, std::string> TokenPlayback::sync(
        const State& state,
        const rules::GameState& gameState,
        display::Screen2D desiredView,
        engine::SequencePlayback& playback)
    {
        const auto desired = desiredTokens(state, gameState, desiredView);
        if (!desired) return std::unexpected(desired.error());
        auto specs = *desired;
        std::array<std::shared_ptr<const sequence::SequenceProgram>, 2> programs{};

        for (std::size_t index = 0; index < specs.size(); ++index)
        {
            if (specs[index].id == data::EmptyDataId ||
                specs[index].id == current_[index])
                continue;

            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), specs[index].id);
            if (!loaded)
                return std::unexpected(loaded.error().detail);
            programs[index] = std::move(*loaded);
        }

        if (specs[1].id != data::EmptyDataId &&
            specs[1].id != current_[1])
        {
            const auto width = anyBitmapWidth(*programs[1]);
            if (!width)
                return std::unexpected(width.error());
            if (*width > static_cast<std::uint32_t>(TradeTokenRightEdge))
                return std::unexpected("Trade token bitmap is wider than right alignment edge");
            specs[1].x = TradeTokenRightEdge - static_cast<std::int32_t>(*width);
        }

        std::vector<sequence::SequenceCommand> commands;

        for (std::size_t index = 0; index < specs.size(); ++index)
        {
            if (specs[index].id == current_[index])
                continue;

            if (current_[index] != data::EmptyDataId)
            {
                commands.push_back(sequence::StopSequenceCommand{
                    current_[index], specs[index].priority, false});
            }

            if (specs[index].id != data::EmptyDataId)
            {
                commands.push_back(sequence::StartSequenceCommand{
                    programs[index], specs[index].priority, {}});
                commands.push_back(sequence::makeMoveXY(
                    specs[index].id, specs[index].priority,
                    specs[index].x, specs[index].y));
            }
        }

        if (commands.size() > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit Trade token transition");
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
                return std::unexpected("validated Trade token command rejected");
        }

        for (std::size_t index = 0; index < specs.size(); ++index)
            current_[index] = specs[index].id;
        return {};
    }
}
