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
        inline constexpr std::array<int, 4> MenuButtonX{31, 180, 478, 615};
        inline constexpr std::array<int, 4> MenuButtonY{494, 493, 490, 490};
        inline constexpr std::array<int, 4> MenuButtonWidth{170, 159, 164, 175};
        inline constexpr std::array<int, 4> MenuButtonHeight{59, 60, 63, 62};
        inline constexpr int OptionOkayX = 350;
        inline constexpr int OptionOkayY = 450;
        inline constexpr int OptionOkayWidth = 127;
        inline constexpr int OptionOkayHeight = 36;
    }

    Rect menuButtonRect(MenuButton button) noexcept
    {
        const auto index = static_cast<std::size_t>(button);
        if (index >= MenuButtonX.size())
            return {};
        return {MenuButtonX[index], MenuButtonY[index],
            MenuButtonX[index] + MenuButtonWidth[index],
            MenuButtonY[index] + MenuButtonHeight[index]};
    }

    std::optional<MenuButton> menuButtonHit(int x, int y) noexcept
    {
        for (std::size_t index = 0; index < MenuButtonX.size(); ++index)
        {
            const auto button = static_cast<MenuButton>(index);
            if (menuButtonRect(button).contains(x, y))
                return button;
        }
        return std::nullopt;
    }

    Rect optionOkayRect() noexcept
    {
        return {OptionOkayX, OptionOkayY,
            OptionOkayX + OptionOkayWidth,
            OptionOkayY + OptionOkayHeight};
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
        if (!state.active || message.type != uimsg::Type::MouseLeftDown)
            return result;

        const auto menu = menuButtonHit(
            static_cast<int>(message.numberA),
            static_cast<int>(message.numberB));
        if (menu)
        {
            result.pressedMenuButton = menu;
            state.currentScreen = static_cast<Screen>(static_cast<std::uint8_t>(*menu));
            return result;
        }

        if (state.currentScreen == Screen::Option)
        {
            const auto x = static_cast<int>(message.numberA);
            const auto y = static_cast<int>(message.numberB);
            if (optionOkayRect().contains(x, y))
            {
                result.pressedOptionOkay = true;
                result.requestedBackdrop = state.previousView;
                state.active = false;
            }
            return result;
        }

        if (state.currentScreen != Screen::File)
            return result;

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
