#include "StatsCalculatorPlayback.hpp"

#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::statsui
{
    namespace
    {
        [[nodiscard]] constexpr data::DataId mainId(data::DataTag tag) noexcept
        {
            return data::packDataId(data::LegacyGroupId::Main, tag);
        }

        [[nodiscard]] constexpr data::DataId languageId(data::DataTag tag) noexcept
        {
            return data::packDataId(data::LegacyGroupId::LanguageGraphics, tag);
        }

        [[nodiscard]] std::expected<
            std::pair<data::DataId, std::shared_ptr<const sequence::SequenceProgram>>,
            std::string> loadEnter(engine::SequencePlayback& playback)
        {
            for (const auto tag : {CalculatorEnterIdleTag,
                                   CalculatorEnterIdleAlternateTag})
            {
                const auto id = languageId(tag);
                auto loaded = sequence::SequenceProgram::load(playback.resources(), id);
                if (loaded)
                    return std::pair{id, std::move(*loaded)};
            }
            return std::unexpected(
                "UDStats calculator Enter sequence missing from language bank");
        }
    }

    void CalculatorPlayback::reset() noexcept
    {
        published_.fill(std::nullopt);
        visible_ = false;
    }

    std::expected<void, std::string> CalculatorPlayback::sync(
        display::Screen2D desiredView, engine::SequencePlayback& playback)
    {
        const bool desiredVisible = desiredView == display::Screen2D::Portfolio;
        std::array<std::optional<Published>, SlotCount> desired{};
        std::optional<std::pair<data::DataId,
            std::shared_ptr<const sequence::SequenceProgram>>> enterProgram;
        if (desiredVisible)
        {
            desired[0] = Published{mainId(CalculatorBackgroundTag),
                CalculatorBackgroundPriority};
            desired[1] = Published{mainId(CalculatorTextBoxTag),
                CalculatorBackgroundPriority};
            for (std::size_t index = 0; index < 10; ++index)
                desired[2 + index] = Published{mainId(static_cast<data::DataTag>(
                    CalculatorNumberIdleBaseTag + index)), CalculatorButtonPriority};
            data::DataId enterId{};
            if (published_[12])
                enterId = published_[12]->id;
            else
            {
                auto loaded = loadEnter(playback);
                if (!loaded) return std::unexpected(loaded.error());
                enterProgram = std::move(*loaded);
                enterId = enterProgram->first;
            }
            desired[12] = Published{enterId, CalculatorButtonPriority};
            for (std::size_t index = 0; index < 8; ++index)
                desired[13 + index] = Published{mainId(static_cast<data::DataTag>(
                    CalculatorFunctionIdleBaseTag + index)), CalculatorButtonPriority};
        }

        std::array<std::shared_ptr<const sequence::SequenceProgram>, SlotCount> programs{};
        std::size_t commandCount{};
        for (std::size_t slot = 0; slot < SlotCount; ++slot)
        {
            if (published_[slot] == desired[slot]) continue;
            if (published_[slot]) ++commandCount;
            if (!desired[slot]) continue;

            if (slot == 12 && enterProgram)
                programs[slot] = enterProgram->second;
            else
            {
                auto loaded = sequence::SequenceProgram::load(
                    playback.resources(), desired[slot]->id);
                if (!loaded)
                    return std::unexpected(
                        "UDStats calculator sequence failed: " + loaded.error().detail);
                programs[slot] = std::move(*loaded);
            }
            ++commandCount;
        }

        if (commandCount > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit UDStats calculator transition");
        }

        std::vector<sequence::SequenceCommand> commands;
        commands.reserve(commandCount);
        for (std::size_t slot = 0; slot < SlotCount; ++slot)
        {
            if (published_[slot] == desired[slot]) continue;
            if (published_[slot])
                commands.push_back(sequence::StopSequenceCommand{
                    published_[slot]->id, published_[slot]->priority, false});
            if (desired[slot])
                commands.push_back(sequence::StartSequenceCommand{
                    programs[slot], desired[slot]->priority, {}});
        }

        for (auto& command : commands)
        {
            const auto queued = std::visit(
                [&](auto value)
                {
                    return playback.commands().enqueue(std::move(value));
                }, std::move(command));
            if (!queued)
                return std::unexpected(
                    "validated UDStats calculator command rejected");
        }

        published_ = desired;
        visible_ = desiredVisible;
        return {};
    }
}
