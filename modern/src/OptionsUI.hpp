#pragma once

#include "Display.hpp"
#include "UIMessages.hpp"

#include <cstdint>
#include <optional>

namespace monopoly::optionsui
{
    enum class Screen : std::uint8_t
    {
        File = 0,
        Option,
        Credits,
        Help,
        LoadBoard,
        LoadGame
    };

    enum class FileButton : std::uint8_t
    {
        NewGame = 0,
        Load,
        Save,
        Exit,
        Cancel,
        Count
    };

    struct Rect
    {
        int left{};
        int top{};
        int right{};
        int bottom{};

        [[nodiscard]] bool contains(int x, int y) const noexcept
        {
            return x >= left && x < right && y >= top && y < bottom;
        }
    };

    struct State
    {
        Screen currentScreen{Screen::File};
        display::Screen2D previousView{display::Screen2D::Main};
        bool active{};
    };

    struct InputResult
    {
        std::optional<display::Screen2D> requestedBackdrop;
        std::optional<FileButton> pressedFileButton;
    };

    [[nodiscard]] Rect fileButtonRect(FileButton button) noexcept;
    [[nodiscard]] std::optional<FileButton> fileButtonHit(int x, int y) noexcept;

    [[nodiscard]] bool beginFromIBar(
        State& state,
        display::Screen2D previousView) noexcept;

    [[nodiscard]] InputResult processInput(
        State& state,
        display::Screen2D desiredView,
        const uimsg::Message& message) noexcept;

    void reset(State& state) noexcept;
}
