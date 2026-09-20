#pragma once

#include "Display.hpp"
#include "UIMessages.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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

    inline constexpr std::uint8_t MusicTuneCount = 5;

    inline constexpr std::array<OptionToggle, 8> SupportedOptionToggles{
        OptionToggle::TokenVoices, OptionToggle::HostComments, OptionToggle::Music,
        OptionToggle::TokenAnimations, OptionToggle::Camera, OptionToggle::Lighting,
        OptionToggle::Board3D, OptionToggle::Filtering};

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
        std::uint8_t musicTuneIndex{};
        std::uint8_t originalMusicTuneIndex{};
        bool optionSnapshotLoaded{};
        bool active{};
        std::array<Rect, MusicTuneCount> musicChoiceRects{};
        bool quickHelpVisible{};
        std::string quickHelpText;
        std::vector<std::string> quickHelpLines;
        std::size_t quickHelpFirstLine{};
        std::size_t quickHelpLinesPerPage{1};
        std::array<Rect, 3> quickHelpButtonRects{};
    };

    struct InputResult
    {
        std::optional<display::Screen2D> requestedBackdrop;
        std::optional<MenuButton> pressedMenuButton;
        std::optional<FileButton> pressedFileButton;
        std::optional<HelpButton> pressedHelpButton;
        std::optional<OptionToggle> pressedOptionToggle;
        bool pressedOptionOkay{};
        std::optional<std::uint8_t> pressedMusicTune;
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
        bool tokenVoicesOn, bool hostCommentsOn,
        bool musicOn, std::uint8_t musicTuneIndex,
        bool tokenAnimationsOn, bool cameraMovementOn,
        bool lightingOn, bool board3DOn,
        bool filteringOn = true) noexcept;

    [[nodiscard]] bool selectMusicTune(State& state, std::uint8_t tuneIndex) noexcept;

    [[nodiscard]] InputResult processInput(
        State& state,
        display::Screen2D desiredView,
        const uimsg::Message& message) noexcept;

    void reset(State& state) noexcept;
}
