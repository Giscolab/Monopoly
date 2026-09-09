#include "OptionsTogglePlayback.hpp"

#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::optionsui
{
    namespace
    {
        [[nodiscard]] std::optional<std::size_t> supportedIndex(
            OptionToggle toggle) noexcept
        {
            for (std::size_t index = 0; index < SupportedOptionToggles.size(); ++index)
                if (SupportedOptionToggles[index] == toggle) return index;
            return std::nullopt;
        }

        struct LoadedPair
        {
            std::shared_ptr<const sequence::SequenceProgram> on;
            std::shared_ptr<const sequence::SequenceProgram> off;
        };
    }

    std::int8_t TogglePlayback::shown(OptionToggle toggle) const noexcept
    {
        const auto index = supportedIndex(toggle);
        return index ? shown_[*index] : static_cast<std::int8_t>(-1);
    }

    std::expected<void, std::string> TogglePlayback::sync(
        const State& state,
        display::Screen2D desiredView,
        engine::SequencePlayback& playback)
    {
        const bool visible = desiredView == display::Screen2D::Options &&
            state.active && state.currentScreen == Screen::Option &&
            state.optionSnapshotLoaded;

        std::array<std::int8_t, SupportedOptionToggles.size()> desired{};
        desired.fill(-1);
        if (visible)
        {
            for (std::size_t index = 0; index < SupportedOptionToggles.size(); ++index)
            {
                const auto toggle = SupportedOptionToggles[index];
                desired[index] = state.optionOn[static_cast<std::size_t>(toggle)] ? 1 : 0;
            }
        }

        if (desired == shown_) return {};
        std::array<LoadedPair, SupportedOptionToggles.size()> programs{};
        for (std::size_t index = 0; index < desired.size(); ++index)
        {
            if (desired[index] == shown_[index] || desired[index] < 0) continue;
            const bool value = desired[index] != 0;
            auto on = sequence::SequenceProgram::load(
                playback.resources(), toggleSequence(true, value));
            if (!on) return std::unexpected(on.error().detail);
            auto off = sequence::SequenceProgram::load(
                playback.resources(), toggleSequence(false, value));
            if (!off) return std::unexpected(off.error().detail);
            programs[index].on = std::move(*on);
            programs[index].off = std::move(*off);
        }

        std::vector<sequence::SequenceCommand> commands;
        for (std::size_t index = 0; index < desired.size(); ++index)
        {
            if (desired[index] == shown_[index]) continue;
            const auto toggle = SupportedOptionToggles[index];
            const auto priority = togglePriority(toggle);
            if (shown_[index] >= 0)
            {
                const bool oldValue = shown_[index] != 0;
                commands.push_back(sequence::StopSequenceCommand{
                    toggleSequence(true, oldValue), priority, false});
                commands.push_back(sequence::StopSequenceCommand{
                    toggleSequence(false, oldValue), priority, false});
            }
            if (desired[index] < 0) continue;
            const bool value = desired[index] != 0;
            const auto onRect = optionToggleRect(toggle, true);
            const auto offRect = optionToggleRect(toggle, false);

            commands.push_back(sequence::StartSequenceCommand{
                programs[index].on, priority, {}});
            commands.push_back(sequence::makeMoveXY(
                toggleSequence(true, value), priority, onRect.left, onRect.top));
            commands.push_back(sequence::SetSequenceEndingActionCommand{
                toggleSequence(true, value), priority, ToggleStayAtEnd, false});

            commands.push_back(sequence::StartSequenceCommand{
                programs[index].off, priority, {}});
            commands.push_back(sequence::makeMoveXY(
                toggleSequence(false, value), priority, offRect.left, offRect.top));
            commands.push_back(sequence::SetSequenceEndingActionCommand{
                toggleSequence(false, value), priority, ToggleStayAtEnd, false});
        }

        if (commands.size() > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
            return std::unexpected(
                "sequence command queue cannot fit Options toggle transition");

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
                    "validated Options toggle command rejected");
        }

        shown_ = desired;
        return {};
    }
}
