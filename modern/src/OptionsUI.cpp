#include "OptionsUI.hpp"

#include <array>

namespace monopoly::optionsui
{
    namespace
    {
        inline constexpr std::array<int, 5> FileButtonY{
            114, 180, 247, 314, 401};
        inline constexpr int FileButtonX = 291;
        inline constexpr int FileButtonWidth = 220;
        inline constexpr int FileButtonHeight = 62;
    }

    Rect fileButtonRect(FileButton button) noexcept
    {
        const auto index = static_cast<std::size_t>(button);
        if (index >= FileButtonY.size())
            return {};
        const int top = FileButtonY[index];
        return {FileButtonX, top,
            FileButtonX + FileButtonWidth,
            top + FileButtonHeight};
    }

    std::optional<FileButton> fileButtonHit(int x, int y) noexcept
    {
        for (std::size_t index = 0; index < FileButtonY.size(); ++index)
        {
            const auto button = static_cast<FileButton>(index);
            if (fileButtonRect(button).contains(x, y))
                return button;
        }
        return std::nullopt;
    }

    bool beginFromIBar(
        State& state,
        display::Screen2D previousView) noexcept
    {
        if (!display::isIBarVisible(previousView))
            return false;
        state.currentScreen = Screen::File;
        state.previousView = previousView;
        state.active = true;
        return true;
    }

    InputResult processInput(
        State& state,
        display::Screen2D desiredView,
        const uimsg::Message& message) noexcept
    {
        InputResult result{};
        if (desiredView != display::Screen2D::Options)
        {
            state.active = false;
            return result;
        }
        if (!state.active || state.currentScreen != Screen::File ||
            message.type != uimsg::Type::MouseLeftDown)
        {
            return result;
        }

        const auto button = fileButtonHit(
            static_cast<int>(message.numberA),
            static_cast<int>(message.numberB));
        if (!button)
            return result;

        result.pressedFileButton = button;
        if (*button == FileButton::Cancel)
        {
            result.requestedBackdrop = state.previousView;
            state.active = false;
        }
        return result;
    }

    void reset(State& state) noexcept
    {
        state = State{};
    }
}
