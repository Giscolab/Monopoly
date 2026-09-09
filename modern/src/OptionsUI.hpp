#pragma once

#include "Display.hpp"
#include "UIMessages.hpp"

#include <array>
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

    enum class MenuButton : std::uint8_t
    {
        File = 0,
        Option,
        Credits,
        Help,
        Count
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

    enum class HelpButton : std::uint8_t
    {
        QuickHelp = 0,
        FullHelp,
        Cancel,
        Count
    };

    enum class OptionToggle : std::uint8_t
    {
        TokenVoices = 0,
        HostComments,
        Music,
        TokenAnimations,
        Camera,
        Lighting,
        Board3D,
        Filtering,
        Dithering,
        Resolution,
        Count
    };

    inline constexpr std::array<OptionToggle, 4> SupportedOptionToggles{
        OptionToggle::TokenAnimations, OptionToggle::Camera,
        OptionToggle::Lighting, OptionToggle::Board3D};

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
        std::array<bool, static_cast<std::size_t>(OptionToggle::Count)> optionOn{};
        bool optionSnapshotLoaded{};
        bool active{};
    };

    struct InputResult
    {
        std::optional<display::Screen2D> requestedBackdrop;
        std::optional<MenuButton> pressedMenuButton;
        std::optional<FileButton> pressedFileButton;
        std::optional<HelpButton> pressedHelpButton;
        std::optional<OptionToggle> pressedOptionToggle;
        bool pressedOptionOkay{};
    };

    [[nodiscard]] Rect menuButtonRect(MenuButton button) noexcept;
    [[nodiscard]] std::optional<MenuButton> menuButtonHit(int x, int y) noexcept;
    [[nodiscard]] Rect optionOkayRect() noexcept;
    [[nodiscard]] Rect optionToggleRect(OptionToggle toggle, bool onSide) noexcept;
    [[nodiscard]] bool optionToggleSupported(OptionToggle toggle) noexcept;
    [[nodiscard]] Rect fileButtonRect(FileButton button) noexcept;
    [[nodiscard]] std::optional<FileButton> fileButtonHit(int x, int y) noexcept;
    [[nodiscard]] Rect helpButtonRect(HelpButton button) noexcept;
    [[nodiscard]] std::optional<HelpButton> helpButtonHit(int x, int y) noexcept;

    [[nodiscard]] bool beginFromIBar(
        State& state,
        display::Screen2D previousView) noexcept;

    void loadSupportedOptionValues(State& state,
        bool tokenAnimationsOn, bool cameraMovementOn,
        bool lightingOn, bool board3DOn) noexcept;

    [[nodiscard]] InputResult processInput(
        State& state,
        display::Screen2D desiredView,
        const uimsg::Message& message) noexcept;

    void reset(State& state) noexcept;
}
