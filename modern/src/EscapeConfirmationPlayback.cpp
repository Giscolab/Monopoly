#include "EscapeConfirmationPlayback.hpp"

#include <array>
#include <memory>
#include <utility>

namespace monopoly::ibar
{
    namespace
    {
        struct Item
        {
            data::DataId id{};
            std::uint16_t priority{};
            int x{};
            int y{};
        };

        [[nodiscard]] constexpr data::DataId languageGraphic(
            data::DataTag tag) noexcept
        {
            return data::packDataId(data::LegacyGroupId::LanguageGraphics, tag);
        }

        [[nodiscard]] constexpr std::array<Item, 3> items(bool usa) noexcept
        {
            const auto ask = usa ? EscapeAskUsaTag : EscapeAskEuropeTag;
            const auto yes = usa ? EscapeYesUsaTag : EscapeYesEuropeTag;
            const auto no = usa ? EscapeNoUsaTag : EscapeNoEuropeTag;
            return {{
                {languageGraphic(ask), EscapeMenuPriority, 289, 83},
                {languageGraphic(yes), static_cast<std::uint16_t>(EscapeMenuPriority + 1), 299, 183},
                {languageGraphic(no), static_cast<std::uint16_t>(EscapeMenuPriority + 1), 419, 183}
            }};
        }
    }

    std::expected<void, std::string> EscapeConfirmationPlayback::sync(
        const State& state, bool usaEdition,
        engine::SequencePlayback& playback)
    {
        const bool desiredVisible = state.escapeMenuUp;
        if (desiredVisible == visible_ &&
            (!desiredVisible || usaEdition == usaEdition_))
        {
            return {};
        }

        const auto previous = items(usaEdition_);
        const auto desired = items(usaEdition);

        std::array<std::shared_ptr<const sequence::SequenceProgram>, 3> programs{};
        if (desiredVisible)
        {
            for (std::size_t index = 0; index < desired.size(); ++index)
            {
                auto loaded = sequence::SequenceProgram::load(
                    playback.resources(), desired[index].id);
                if (!loaded)
                    return std::unexpected(
                        "Escape confirmation resource failed: " + loaded.error().detail);
                programs[index] = std::move(*loaded);
            }
        }

        const bool replaceVisible = visible_ &&
            (!desiredVisible || usaEdition != usaEdition_);
        const std::size_t stopCount = replaceVisible ? previous.size() : 0U;
        const std::size_t startCount = desiredVisible ? desired.size() : 0U;
        if (stopCount + startCount > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit Escape confirmation transition");
        }

        if (replaceVisible)
        {
            for (const auto& item : previous)
            {
                if (!playback.commands().enqueue(sequence::StopSequenceCommand{
                        item.id, item.priority, false}))
                    return std::unexpected(
                        "validated Escape confirmation stop rejected");
            }
        }

        if (desiredVisible)
        {
            for (std::size_t index = 0; index < desired.size(); ++index)
            {
                const auto& item = desired[index];
                if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                        programs[index], item.priority, {},
                        sequence::moveXYTransform(item.x, item.y)}))
                    return std::unexpected(
                        "validated Escape confirmation start rejected");
            }
        }

        visible_ = desiredVisible;
        usaEdition_ = usaEdition;
        return {};
    }
}
