#include "StatsPlayback.hpp"

#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::statsui
{
    namespace
    {
        [[nodiscard]] constexpr std::size_t visualColumn(ButtonVisual visual) noexcept
        {
            return visual == ButtonVisual::Idle ? 0U
                : visual == ButtonVisual::Return ? 1U : 2U;
        }

        [[nodiscard]] constexpr std::uint16_t categoryPriority(
            std::size_t index, ButtonVisual visual) noexcept
        {
            return visual == ButtonVisual::Press
                ? CategoryPressPriorities[index]
                : CategoryIdlePriorities[index];
        }

        [[nodiscard]] constexpr std::uint16_t sortPriority(
            std::size_t index, ButtonVisual visual) noexcept
        {
            return visual == ButtonVisual::Press
                ? SortPressPriorities[index]
                : SortIdlePriorities[index];
        }
    }

    ButtonVisual Playback::categoryVisual(std::size_t index) const noexcept
    {
        return index < categoryVisual_.size() ? categoryVisual_[index]
                                              : ButtonVisual::Off;
    }

    ButtonVisual Playback::sortVisual(std::size_t index) const noexcept
    {
        return index < sortVisual_.size() ? sortVisual_[index]
                                          : ButtonVisual::Off;
    }

    void Playback::reset() noexcept
    {
        published_.fill(std::nullopt);
        categoryVisual_.fill(ButtonVisual::Off);
        sortVisual_.fill(ButtonVisual::Off);
        visible_ = false;
        screen_ = Screen::Player;
        sort_ = 0;
    }

    std::expected<void, std::string> Playback::sync(
        const State& state, display::Screen2D desiredView,
        engine::SequencePlayback& playback)
    {
        const bool desiredVisible = desiredView == display::Screen2D::Portfolio;
        const auto desiredScreen = state.screen;
        const auto desiredSort = static_cast<std::uint8_t>(state.activeSort < 4
            ? state.activeSort : 0);

        auto nextCategory = categoryVisual_;
        auto nextSort = sortVisual_;
        if (!desiredVisible)
        {
            nextCategory.fill(ButtonVisual::Off);
            nextSort.fill(ButtonVisual::Off);
        }
        else if (!visible_)
        {
            nextCategory.fill(ButtonVisual::Idle);
            nextSort.fill(ButtonVisual::Idle);
            nextCategory[static_cast<std::size_t>(desiredScreen)] = ButtonVisual::Press;
            nextSort[desiredSort] = ButtonVisual::Press;
        }
        else if (desiredScreen != screen_)
        {
            nextCategory[static_cast<std::size_t>(screen_)] = ButtonVisual::Return;
            nextCategory[static_cast<std::size_t>(desiredScreen)] = ButtonVisual::Press;
            nextSort.fill(ButtonVisual::Idle);
            nextSort[desiredSort] = ButtonVisual::Press;
        }
        else if (desiredSort != sort_)
        {
            nextSort[sort_] = ButtonVisual::Return;
            nextSort[desiredSort] = ButtonVisual::Press;
        }

        std::array<std::optional<Published>, SlotCount> desired{};
        if (desiredVisible)
        {
            desired[0] = Published{
                languageSequence(StatusBackgroundTag), StatusBackgroundPriority};
            if (desiredScreen == Screen::Deed)
            {
                desired[1] = Published{
                    mainSequence(DeedBackgroundTag), DeedBackgroundPriority,
                    5, 229, true, false};
            }
            else if (desiredScreen == Screen::Bank)
            {
                desired[1] = Published{
                    mainSequence(BankBackgroundTags[desiredSort]),
                    BankBackgroundPriority};
            }

            desired[2] = Published{
                languageSequence(CategoryBarTags[0]), StatusBarPriority};
            desired[3] = Published{
                languageSequence(CategoryBarTags[1]), StatusBarOverlayPriority};
            const auto screenIndex = static_cast<std::size_t>(desiredScreen);
            desired[4] = Published{
                languageSequence(SortBarTags[screenIndex][0]), StatusBarPriority};
            desired[5] = Published{
                languageSequence(SortBarTags[screenIndex][1]), StatusBarOverlayPriority};

            for (std::size_t index = 0; index < nextCategory.size(); ++index)
            {
                const auto visual = nextCategory[index];
                if (visual == ButtonVisual::Off) continue;
                desired[6 + index] = Published{
                    languageSequence(CategoryButtonTags[index][visualColumn(visual)]),
                    categoryPriority(index, visual), 0, 0, false,
                    visual == ButtonVisual::Press || visual == ButtonVisual::Return};
            }
            for (std::size_t index = 0; index < nextSort.size(); ++index)
            {
                const auto visual = nextSort[index];
                if (visual == ButtonVisual::Off) continue;
                desired[9 + index] = Published{
                    languageSequence(SortButtonTags[screenIndex][index][visualColumn(visual)]),
                    sortPriority(index, visual), 0, 0, false,
                    visual == ButtonVisual::Press || visual == ButtonVisual::Return};
            }
        }

        std::array<std::shared_ptr<const sequence::SequenceProgram>, SlotCount> programs{};
        std::size_t commandCount{};
        for (std::size_t slot = 0; slot < SlotCount; ++slot)
        {
            if (published_[slot] == desired[slot]) continue;
            if (published_[slot]) ++commandCount;
            if (desired[slot])
            {
                auto loaded = sequence::SequenceProgram::load(
                    playback.resources(), desired[slot]->id);
                if (!loaded)
                    return std::unexpected(
                        "UDStats sequence " + std::to_string(desired[slot]->id) +
                        ": " + loaded.error().detail);
                programs[slot] = std::move(*loaded);
                ++commandCount;
                if (desired[slot]->moved) ++commandCount;
                if (desired[slot]->stay) ++commandCount;
            }
        }

        if (commandCount > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit UDStats playback transition");
        }

        std::vector<sequence::SequenceCommand> commands;
        commands.reserve(commandCount);
        for (std::size_t slot = 0; slot < SlotCount; ++slot)
        {
            if (published_[slot] == desired[slot]) continue;
            if (published_[slot])
            {
                commands.push_back(sequence::StopSequenceCommand{
                    published_[slot]->id, published_[slot]->priority, false});
            }
            if (!desired[slot]) continue;

            const auto& spec = *desired[slot];
            commands.push_back(sequence::StartSequenceCommand{
                programs[slot], spec.priority, {}});
            if (spec.moved)
            {
                commands.push_back(sequence::makeMoveXY(
                    spec.id, spec.priority, spec.x, spec.y));
            }
            if (spec.stay)
            {
                commands.push_back(sequence::SetSequenceEndingActionCommand{
                    spec.id, spec.priority, StatusStayAtEnd, false});
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
                return std::unexpected("validated UDStats playback command rejected");
        }

        published_ = desired;
        categoryVisual_ = nextCategory;
        sortVisual_ = nextSort;
        visible_ = desiredVisible;
        screen_ = desiredScreen;
        sort_ = desiredSort;
        return {};
    }
}