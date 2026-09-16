#include "StatsFutureImmunityPlayback.hpp"

#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::statsui
{
    namespace
    {
        [[nodiscard]] constexpr data::DataId languageId(
            data::DataTag tag) noexcept
        {
            return data::packDataId(
                data::LegacyGroupId::LanguageGraphics, tag);
        }

        [[nodiscard]] constexpr data::DataId mainId(
            data::DataTag tag) noexcept
        {
            return data::packDataId(data::LegacyGroupId::Main, tag);
        }
    }

    void FutureImmunityPlayback::reset() noexcept
    {
        published_.fill(std::nullopt);
    }
    std::expected<void, std::string> FutureImmunityPlayback::sync(
        const FutureImmunityState& state,
        display::Screen2D desiredView,
        engine::SequencePlayback& playback)
    {
        std::array<std::optional<Published>, SlotCount> desired{};
        std::array<std::optional<sequence::SequenceTransform>, SlotCount>
            transforms{};

        if (desiredView == display::Screen2D::Portfolio && state.open)
        {
            desired[0] = Published{languageId(FutureImmunityFrameTag),
                FutureImmunityPopupPriority};
            desired[1] = Published{languageId(FutureImmunityBoardTag),
                FutureImmunityContentPriority};
            transforms[1] = sequence::moveXYTransform(
                FutureImmunityPopupRect.left, FutureImmunityPopupRect.top);

            if (state.downEnabled)
            {
                desired[2] = Published{mainId(FutureImmunityDownTag),
                    FutureImmunityArrowPriority};
                transforms[2] = sequence::moveXYTransform(25, -214);
            }
            if (state.upEnabled)
            {
                desired[3] = Published{mainId(FutureImmunityUpTag),
                    FutureImmunityArrowPriority};
                transforms[3] = sequence::moveXYTransform(25, -220);
            }
        }

        std::array<std::shared_ptr<const sequence::SequenceProgram>, SlotCount>
            programs{};
        std::size_t commandCount{};
        for (std::size_t slot = 0; slot < SlotCount; ++slot)
        {
            if (published_[slot] == desired[slot]) continue;
            if (published_[slot]) ++commandCount;
            if (!desired[slot]) continue;

            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), desired[slot]->id);
            if (!loaded)
                return std::unexpected(
                    "UDStats Future/Immunity sequence failed: " +
                    loaded.error().detail);
            programs[slot] = std::move(*loaded);
            ++commandCount;
        }
        if (commandCount > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit UDStats Future/Immunity transition");
        }

        std::vector<sequence::SequenceCommand> commands;
        commands.reserve(commandCount);
        for (std::size_t slot = 0; slot < SlotCount; ++slot)
        {
            if (published_[slot] == desired[slot]) continue;
            if (published_[slot])
            {
                commands.push_back(sequence::StopSequenceCommand{
                    published_[slot]->id,
                    published_[slot]->priority,
                    false});
            }
            if (desired[slot])
            {
                commands.push_back(sequence::StartSequenceCommand{
                    programs[slot], desired[slot]->priority, {}, transforms[slot]});
            }
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
                    "validated UDStats Future/Immunity command rejected");
        }

        published_ = desired;
        return {};
    }
}
