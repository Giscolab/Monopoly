#include "StatsCalculatorPlayback.hpp"

#include <algorithm>
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

        [[nodiscard]] sequence::Matrix2D calculatorTokenTransform(
            int x, int y) noexcept
        {
            auto transform = sequence::identity2D();
            transform.values[0] = CalculatorTokenScale;
            transform.values[4] = CalculatorTokenScale;
            transform.values[6] = static_cast<float>(x);
            transform.values[7] = static_cast<float>(y);
            return transform;
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
        display::Screen2D desiredView, const CalculatorUIState& ui,
        const rules::GameState& gameState,
        engine::SequencePlayback& playback)
    {
        const bool desiredVisible = desiredView == display::Screen2D::Portfolio;
        std::array<std::optional<Published>, SlotCount> desired{};
        std::array<std::optional<sequence::SequenceTransform>, SlotCount>
            initialTransforms{};
        std::optional<std::pair<data::DataId,
            std::shared_ptr<const sequence::SequenceProgram>>> enterProgram;
        if (desiredVisible)
        {
            desired[0] = Published{mainId(CalculatorBackgroundTag),
                CalculatorBackgroundPriority};
            desired[1] = Published{mainId(CalculatorTextBoxTag),
                CalculatorBackgroundPriority};

            const bool numberButtonsVisible =
                ui.picker != CalculatorPicker::Player;
            if (numberButtonsVisible)
            {
                for (std::size_t index = 0; index < 10; ++index)
                {
                    const auto base = ui.pressedNumber && *ui.pressedNumber == index
                        ? CalculatorNumberPressBaseTag
                        : CalculatorNumberIdleBaseTag;
                    desired[2 + index] = Published{mainId(
                        static_cast<data::DataTag>(base + index)),
                        CalculatorButtonPriority};
                }

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
            }

            for (std::size_t index = 0; index < 8; ++index)
            {
                const bool pressed = ui.activeFunction &&
                    static_cast<std::uint8_t>(*ui.activeFunction) == index;
                const auto base = pressed
                    ? CalculatorFunctionPressBaseTag
                    : CalculatorFunctionIdleBaseTag;
                desired[13 + index] = Published{mainId(
                    static_cast<data::DataTag>(base + index)),
                    CalculatorButtonPriority};
            }

            if (ui.picker == CalculatorPicker::Player)
            {
                const auto count = std::min<std::size_t>(
                    gameState.numberOfPlayers, rules::MaxPlayers);
                for (std::size_t index = 0; index < count; ++index)
                {
                    const auto token = gameState.players[index].token;
                    if (token >= rules::MaxTokens)
                        return std::unexpected(
                            "UDStats calculator token index is out of range");
                    const auto slot = 21 + index;
                    desired[slot] = Published{mainId(static_cast<data::DataTag>(
                        CalculatorTokenBaseTag + token)), CalculatorButtonPriority};
                    const auto rect = calculatorTokenRect(
                        static_cast<rules::PlayerNumber>(index));
                    initialTransforms[slot] = sequence::SequenceTransform{
                        calculatorTokenTransform(rect.left, rect.top)};
                }
            }
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
                    programs[slot], desired[slot]->priority, {},
                    initialTransforms[slot]});
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
