#include "OptionsUI.hpp"

#include <algorithm>
#include <array>

namespace monopoly::optionsui
{
    std::size_t quickHelpPageLineCount(std::size_t totalLines,
        std::size_t firstLine, int lineHeight, bool initialPage) noexcept
    {
        if (firstLine >= totalLines || lineHeight <= 0) return 0;
        const int availableHeight = 450 - lineHeight;
        if (availableHeight <= 0) return 0;
        // PrintString's strict y < max_height test draws the row whose top is
        // inside the area. The initial page has no GetStringHeight preflight.
        const auto capacity = static_cast<std::size_t>((availableHeight - 1) / lineHeight + 1);
        const auto probeRows = std::min(totalLines - firstLine, capacity);
        if (!initialPage && probeRows * static_cast<std::size_t>(lineHeight) >
                static_cast<std::size_t>(availableHeight))
        {
            // UDOpts Previous/Next reserve another line after an overflowing
            // probe. This applies to Previous returning to page zero as well.
            return std::min(totalLines - firstLine, capacity - 1);
        }
        return probeRows;
    }

    namespace
    {
        inline constexpr std::array<int, 5> FileButtonY{
            114, 180, 247, 314, 401};
        inline constexpr std::array<int, 3> HelpButtonY{180, 288, 401};
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
        inline constexpr int ToggleButtonWidth = 59;
        inline constexpr int ToggleButtonHeight = 33;
        inline constexpr std::array<int, 7> ToggleY{135, 175, 215, 255, 295, 335, 375};
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

    Rect optionToggleRect(OptionToggle toggle, bool onSide) noexcept
    {
        const auto index = static_cast<std::size_t>(toggle);
        if (index >= static_cast<std::size_t>(OptionToggle::Count))
            return {};
        const bool soundColumn = index <= static_cast<std::size_t>(OptionToggle::Music);
        const int columnX = soundColumn ? 25 : 457;
        const std::size_t yIndex = soundColumn ? index : index - 3U;
        if (yIndex >= ToggleY.size()) return {};
        const int left = columnX + (onSide ? 0 : ToggleButtonWidth);
        const int top = ToggleY[yIndex];
        return {left, top, left + ToggleButtonWidth, top + ToggleButtonHeight};
    }

    bool optionToggleSupported(OptionToggle toggle) noexcept
    {
        return std::find(SupportedOptionToggles.begin(),
            SupportedOptionToggles.end(), toggle) != SupportedOptionToggles.end();
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

    Rect helpButtonRect(HelpButton button) noexcept
    {
        const auto index = static_cast<std::size_t>(button);
        if (index >= HelpButtonY.size()) return {};
        const int top = HelpButtonY[index];
        return {FileButtonX, top, FileButtonX + FileButtonWidth, top + FileButtonHeight};
    }

    std::optional<HelpButton> helpButtonHit(int x, int y) noexcept
    {
        for (std::size_t index = 0; index < HelpButtonY.size(); ++index)
        {
            const auto button = static_cast<HelpButton>(index);
            if (helpButtonRect(button).contains(x, y)) return button;
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
        state.optionSnapshotLoaded = false;
        state.active = true;
        return true;
    }

    void loadSupportedOptionValues(State& state,
        bool tokenVoicesOn, bool hostCommentsOn,
        bool musicOn, std::uint8_t musicTuneIndex,
        bool tokenAnimationsOn, bool cameraMovementOn,
        bool lightingOn, bool board3DOn,
        bool filteringOn) noexcept
    {
        state.optionOn[static_cast<std::size_t>(OptionToggle::TokenVoices)] = tokenVoicesOn;
        state.optionOn[static_cast<std::size_t>(OptionToggle::HostComments)] = hostCommentsOn;
        state.optionOn[static_cast<std::size_t>(OptionToggle::Music)] = musicOn;
        state.musicTuneIndex = std::min<std::uint8_t>(musicTuneIndex, MusicTuneCount - 1);
        state.originalMusicTuneIndex = state.musicTuneIndex;
        state.optionOn[static_cast<std::size_t>(OptionToggle::TokenAnimations)] = tokenAnimationsOn;
        state.optionOn[static_cast<std::size_t>(OptionToggle::Camera)] = cameraMovementOn;
        state.optionOn[static_cast<std::size_t>(OptionToggle::Lighting)] = lightingOn;
        state.optionOn[static_cast<std::size_t>(OptionToggle::Board3D)] = board3DOn;
        state.optionOn[static_cast<std::size_t>(OptionToggle::Filtering)] = filteringOn;
        state.optionSnapshotLoaded = true;
    }

    bool selectMusicTune(State& state, std::uint8_t tuneIndex) noexcept
    {
        if (!state.optionSnapshotLoaded || tuneIndex >= MusicTuneCount)
            return false;
        state.musicTuneIndex = tuneIndex;
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
            state.optionSnapshotLoaded = false;
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
            state.quickHelpVisible = false;
            state.musicChoiceRects = {};
            state.currentScreen = static_cast<Screen>(static_cast<std::uint8_t>(*menu));
            state.optionSnapshotLoaded = false;
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
                return result;
            }
            if (!state.optionSnapshotLoaded) return result;
            for (std::uint8_t index = 0; index < MusicTuneCount; ++index)
            {
                if (state.musicChoiceRects[index].contains(x, y))
                {
                    state.musicTuneIndex = index;
                    result.pressedMusicTune = index;
                    return result;
                }
            }
            for (const auto toggle : SupportedOptionToggles)
            {
                if (optionToggleRect(toggle, true).contains(x, y) ||
                    optionToggleRect(toggle, false).contains(x, y))
                {
                    result.pressedOptionToggle = toggle;
                    const auto index = static_cast<std::size_t>(toggle);
                    state.optionOn[index] = !state.optionOn[index];
                    return result;
                }
            }
            return result;
        }

        if (state.currentScreen == Screen::Credits && message.numberB >= 0 && message.numberB < 486)
        {
            state.active = false;
            result.requestedBackdrop = state.previousView;
            return result;
        }

        if (state.currentScreen == Screen::Help)
        {
            if (state.quickHelpVisible)
            {
                for (std::size_t index = 0; index < 3; ++index)
                {
                    if (!state.quickHelpButtonRects[index].contains(
                            static_cast<int>(message.numberA), static_cast<int>(message.numberB))) continue;
                    if (index == 0 && state.quickHelpPageIndex > 0)
                    {
                        --state.quickHelpPageIndex;
                        state.quickHelpFirstLine = state.quickHelpPageOffsets[state.quickHelpPageIndex];
                        state.quickHelpInitialPage = false;
                    }
                    else if (index == 1 && state.quickHelpLinesPerPage > 0 &&
                        state.quickHelpFirstLine + state.quickHelpLinesPerPage < state.quickHelpLines.size() &&
                        state.quickHelpPageIndex + 1 < state.quickHelpPageOffsets.size())
                    {
                        state.quickHelpFirstLine += state.quickHelpLinesPerPage;
                        ++state.quickHelpPageIndex;
                        // The renderer reserves this slot before enabling Next;
                        // UI dispatch stays allocation-free/noexcept.
                        state.quickHelpPageOffsets[state.quickHelpPageIndex] = state.quickHelpFirstLine;
                        state.quickHelpInitialPage = false;
                    }
                    else if (index == 2) state.quickHelpVisible = false;
                    return result;
                }
                return result;
            }
            const auto button = helpButtonHit(
                static_cast<int>(message.numberA),
                static_cast<int>(message.numberB));
            if (!button) return result;
            result.pressedHelpButton = button;
            if (*button == HelpButton::Cancel)
            {
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
