#include "OptionsNavigationPlayback.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::optionsui
{
    namespace
    {
        [[nodiscard]] std::optional<MenuButton> menuForScreen(Screen screen) noexcept
        {
            const auto value = static_cast<std::uint8_t>(screen);
            if (value >= static_cast<std::uint8_t>(MenuButton::Count))
                return std::nullopt;
            return static_cast<MenuButton>(value);
        }

        [[nodiscard]] bool anyVisible(
            const std::array<NavigationVisual, 4>& visual) noexcept
        {
            return std::any_of(visual.begin(), visual.end(),
                [](NavigationVisual value) { return value != NavigationVisual::Off; });
        }
    }

    std::expected<void, std::string> NavigationPlayback::sync(
        const State& state,
        display::Screen2D desiredView,
        engine::SequencePlayback& playback)
    {
        const bool visible = desiredView == display::Screen2D::Options && state.active;
        const bool wasVisible = anyVisible(visual_);
        const auto desiredSelected = visible ? menuForScreen(state.currentScreen)
                                             : std::optional<MenuButton>{};

        auto next = visual_;
        if (!visible)
        {
            next.fill(NavigationVisual::Off);
        }
        else if (!wasVisible)
        {
            for (std::size_t index = 0; index < next.size(); ++index)
                next[index] = desiredSelected &&
                        index == static_cast<std::size_t>(*desiredSelected)
                    ? NavigationVisual::Press
                    : NavigationVisual::Idle;
        }
        else if (desiredSelected != selected_)
        {
            if (selected_)
                next[static_cast<std::size_t>(*selected_)] = NavigationVisual::Return;
            if (desiredSelected)
                next[static_cast<std::size_t>(*desiredSelected)] = NavigationVisual::Press;
        }

        if (next == visual_)
        {
            selected_ = desiredSelected;
            return {};
        }

        std::array<std::shared_ptr<const sequence::SequenceProgram>, 4> programs{};
        for (std::size_t index = 0; index < next.size(); ++index)
        {
            if (next[index] == visual_[index] || next[index] == NavigationVisual::Off)
                continue;
            const auto button = static_cast<MenuButton>(index);
            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), navigationSequence(button, next[index]));
            if (!loaded)
                return std::unexpected(loaded.error().detail);
            programs[index] = std::move(*loaded);
        }

        std::vector<sequence::SequenceCommand> commands;
        for (std::size_t index = 0; index < next.size(); ++index)
        {
            if (next[index] == visual_[index])
                continue;

            const auto button = static_cast<MenuButton>(index);
            if (visual_[index] != NavigationVisual::Off)
            {
                commands.push_back(sequence::StopSequenceCommand{
                    navigationSequence(button, visual_[index]),
                    navigationPriority(button, visual_[index]), false});
            }

            if (next[index] != NavigationVisual::Off)
            {
                const auto id = navigationSequence(button, next[index]);
                const auto priority = navigationPriority(button, next[index]);
                commands.push_back(sequence::StartSequenceCommand{
                    programs[index], priority, {}});
                if (wasVisible &&
                    (next[index] == NavigationVisual::Press ||
                     next[index] == NavigationVisual::Return))
                {
                    commands.push_back(sequence::SetSequenceEndingActionCommand{
                        id, priority, NavigationStayAtEnd, false});
                }
            }
        }

        if (commands.size() > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit Options navigation transition");
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
                return std::unexpected(
                    "validated Options navigation command rejected");
        }

        visual_ = next;
        selected_ = desiredSelected;
        return {};
    }
}
