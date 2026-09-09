#include "OptionsFilePlayback.hpp"
#include "OptionsNavigationPlayback.hpp"
#include "OptionsOptionPlayback.hpp"
#include "OptionsTogglePlayback.hpp"
#include "OptionsHelpPlayback.hpp"
#include "OptionsUI.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;

    void require(bool condition, const char* description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << description << '\n';
        if (!condition) throw std::runtime_error(description);
    }

    uimsg::Message click(int x, int y)
    {
        return {uimsg::Type::MouseLeftDown, x, y};
    }

    void testRetailContract()
    {
        require(static_cast<std::uint8_t>(optionsui::Screen::File) == 0 &&
                static_cast<std::uint8_t>(optionsui::Screen::LoadGame) == 5,
            "UDOpts screen enum keeps retail FILE..LOAD_GAME order");
        require(optionsui::fileSequence(optionsui::FileTitleTag) ==
                data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x023A),
            "File title uses retail CNK_syfiles");
        const std::array<data::DataTag, 5> expected{
            0x0241, 0x023E, 0x0244, 0x0238, 0x0235};
        require(optionsui::FileButtonInTags == expected &&
                optionsui::FileScreenPriority == 50 &&
                optionsui::FileScreenStayAtEnd == 2,
            "File buttons use retail incoming CNKs, priority 50 and StayAtEnd");

        const std::array<int, 5> tops{114, 180, 247, 314, 401};
        bool exact = true;
        for (std::size_t index = 0; index < tops.size(); ++index)
        {
            const auto rect = optionsui::fileButtonRect(
                static_cast<optionsui::FileButton>(index));
            exact = exact && rect.left == 291 && rect.top == tops[index] &&
                rect.right == 511 && rect.bottom == tops[index] + 62;
        }
        require(exact,
            "File button hotspots match retail x=291 width=220 and five y rows");
        require(optionsui::fileButtonHit(291, 401) == optionsui::FileButton::Cancel &&
                !optionsui::fileButtonHit(511, 401) &&
                !optionsui::fileButtonHit(291, 463),
            "File hotspots preserve Win32 right/bottom exclusive edges");
    }

    void testFileInput()
    {
        optionsui::State state{};
        require(!optionsui::beginFromIBar(state, display::Screen2D::Options) &&
                !state.active,
            "Options entry rejects non-IBar previous view");
        require(optionsui::beginFromIBar(state, display::Screen2D::Trade) &&
                state.active && state.currentScreen == optionsui::Screen::File &&
                state.previousView == display::Screen2D::Trade,
            "Options entry records exact previous IBar view and starts File screen");

        const auto load = optionsui::processInput(
            state, display::Screen2D::Options, click(300, 190));
        require(load.pressedFileButton == optionsui::FileButton::Load &&
                !load.requestedBackdrop && state.active,
            "Load hit is identified without inventing an unported dialog action");

        const auto cancel = optionsui::processInput(
            state, display::Screen2D::Options, click(300, 420));
        require(cancel.pressedFileButton == optionsui::FileButton::Cancel &&
                cancel.requestedBackdrop == display::Screen2D::Trade &&
                !state.active,
            "File Cancel returns to the exact previous IBar view");

        require(optionsui::beginFromIBar(state, display::Screen2D::Portfolio),
            "Options can reopen from Portfolio");
        (void)optionsui::processInput(
            state, display::Screen2D::Main, click(300, 420));
        require(!state.active,
            "leaving Options clears active Options projection");
    }

    void testFilePlayback()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        optionsui::FilePlayback file;
        optionsui::State state{};
        require(file.sync(state, display::Screen2D::Options, playback) &&
                playback.commands().pendingCount() == 0 && !file.visible(),
            "inactive Options projection publishes no File-screen sequences");

        require(optionsui::beginFromIBar(state, display::Screen2D::Main),
            "File playback fixture enters Options from Main");
        require(file.sync(state, display::Screen2D::Options, playback) &&
                playback.commands().pendingCount() == 12 && file.visible(),
            "File screen queues six Start+StayAtEnd pairs atomically");
        require(playback.update(0).has_value(),
            "File screen opening transition executes");

        const auto title = optionsui::fileSequence(optionsui::FileTitleTag);
        bool allPresent = playback.runtime().matching(
            title, optionsui::FileScreenPriority).size() == 1;
        for (const auto tag : optionsui::FileButtonInTags)
        {
            allPresent = allPresent && playback.runtime().matching(
                optionsui::fileSequence(tag),
                optionsui::FileScreenPriority).size() == 1;
        }
        require(allPresent,
            "File title and five incoming buttons publish at retail priority 50");
        require(playback.update(100).has_value() &&
                playback.runtime().matching(
                    optionsui::fileSequence(optionsui::FileButtonInTags[0]),
                    optionsui::FileScreenPriority).size() == 1,
            "StayAtEnd keeps finite File button incoming sequence on its last frame");
        require(file.sync(state, display::Screen2D::Options, playback) &&
                playback.commands().pendingCount() == 0,
            "unchanged File screen queues no redundant commands");

        const auto cancel = optionsui::processInput(
            state, display::Screen2D::Options, click(300, 420));
        require(cancel.requestedBackdrop == display::Screen2D::Main &&
                file.sync(state, display::Screen2D::Options, playback) &&
                playback.commands().pendingCount() == 6,
            "File Cancel queues one Stop for each published retail sequence");
        require(playback.update(101).has_value() &&
                playback.runtime().matching(
                    title, optionsui::FileScreenPriority).empty(),
            "File Cancel removes File screen from Overlay2D runtime");
    }

    void testNavigationInputAndPlayback()
    {
        const std::array<data::DataTag, 4> idle{0x018E, 0x0192, 0x0186, 0x018A};
        const std::array<data::DataTag, 4> ret{0x018F, 0x0193, 0x0187, 0x018B};
        const std::array<data::DataTag, 4> press{0x0190, 0x0194, 0x0188, 0x018C};
        require(optionsui::NavigationIdleTags == idle &&
                optionsui::NavigationReturnTags == ret &&
                optionsui::NavigationPressTags == press &&
                optionsui::NavigationIdlePriorities == std::array<std::uint16_t, 4>{1006,1008,1004,1001} &&
                optionsui::NavigationPressPriorities == std::array<std::uint16_t, 4>{1007,1009,1005,1003},
            "Options navigation uses exact retail CNKs and idle/press priorities");

        const std::array<int,4> xs{31,180,478,615};
        const std::array<int,4> ys{494,493,490,490};
        const std::array<int,4> widths{170,159,164,175};
        const std::array<int,4> heights{59,60,63,62};
        bool rects = true;
        for (std::size_t i=0; i<4; ++i)
        {
            const auto r=optionsui::menuButtonRect(static_cast<optionsui::MenuButton>(i));
            rects = rects && r.left==xs[i] && r.top==ys[i] &&
                r.right==xs[i]+widths[i] && r.bottom==ys[i]+heights[i];
        }
        require(rects,
            "Options navigation hotspots match retail File/Option/Credits/Help rectangles");
        require(optionsui::menuButtonHit(181, 500) == optionsui::MenuButton::File,
            "retail overlap resolves File before Option in index order");

        optionsui::State state{};
        require(optionsui::beginFromIBar(state, display::Screen2D::Main),
            "navigation fixture enters Options from Main");
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        optionsui::NavigationPlayback nav;
        require(nav.sync(state, display::Screen2D::Options, playback) &&
                playback.commands().pendingCount()==4,
            "Options navigation opening queues four retail starts");
        require(playback.update(0).has_value() &&
                playback.runtime().matching(optionsui::navigationSequence(
                    optionsui::MenuButton::File, optionsui::NavigationVisual::Press),1007).size()==1 &&
                playback.runtime().matching(optionsui::navigationSequence(
                    optionsui::MenuButton::Option, optionsui::NavigationVisual::Idle),1008).size()==1,
            "Options navigation opens with File pressed and other menu buttons idle");

        const auto optionRect=optionsui::menuButtonRect(optionsui::MenuButton::Option);
        const auto optionClick=optionsui::processInput(state, display::Screen2D::Options,
            click((optionRect.left+optionRect.right)/2, optionRect.top+1));
        require(optionClick.pressedMenuButton==optionsui::MenuButton::Option &&
                state.currentScreen==optionsui::Screen::Option,
            "Options navigation click switches projection to Option screen");
        require(nav.sync(state, display::Screen2D::Options, playback) &&
                playback.commands().pendingCount()==6,
            "File-to-Option queues Stop/Return/Stay plus Stop/Press/Stay atomically");
        require(playback.update(1).has_value() &&
                nav.visual(optionsui::MenuButton::File)==optionsui::NavigationVisual::Return &&
                nav.visual(optionsui::MenuButton::Option)==optionsui::NavigationVisual::Press &&
                playback.runtime().matching(optionsui::navigationSequence(
                    optionsui::MenuButton::File, optionsui::NavigationVisual::Return),1006).size()==1,
            "previous File tab uses retail Return while Option becomes Press");

        const auto creditsRect=optionsui::menuButtonRect(optionsui::MenuButton::Credits);
        (void)optionsui::processInput(state, display::Screen2D::Options,
            click(creditsRect.left+1, creditsRect.top+1));
        require(nav.sync(state, display::Screen2D::Options, playback) &&
                playback.commands().pendingCount()==6 && playback.update(2).has_value() &&
                nav.visual(optionsui::MenuButton::File)==optionsui::NavigationVisual::Return &&
                nav.visual(optionsui::MenuButton::Option)==optionsui::NavigationVisual::Return &&
                nav.visual(optionsui::MenuButton::Credits)==optionsui::NavigationVisual::Press,
            "navigation preserves older Return art and transfers Press to Credits");

        (void)optionsui::processInput(state, display::Screen2D::Main, click(0,0));
        require(nav.sync(state, display::Screen2D::Main, playback) &&
                playback.commands().pendingCount()==4 && playback.update(3).has_value(),
            "leaving Options stops exactly the four currently published navigation roots");
    }

    void testOptionScreenFrame()
    {
        require(optionsui::OptionScreenTags == std::array<data::DataTag, 4>{
                    0x0277, 0x0278, 0x0262, 0x026E} &&
                optionsui::OptionScreenPriority == 50 &&
                optionsui::OptionScreenStayAtEnd == 2,
            "Option frame uses retail title/subtitles/OK CNKs and priority 50");
        const auto okayRect = optionsui::optionOkayRect();
        require(okayRect.left == 350 && okayRect.top == 450 &&
                okayRect.right == 477 && okayRect.bottom == 486 &&
                okayRect.contains(350, 450) && !okayRect.contains(477, 450),
            "Option OK hotspot matches retail 350,450 127x36 rectangle");

        optionsui::State state{};
        require(optionsui::beginFromIBar(state, display::Screen2D::Trade),
            "Option-frame fixture enters Options from Trade");
        const auto optionRect = optionsui::menuButtonRect(optionsui::MenuButton::Option);
        const auto tab = optionsui::processInput(state, display::Screen2D::Options,
            click((optionRect.left + optionRect.right) / 2, optionRect.top + 1));
        require(tab.pressedMenuButton == optionsui::MenuButton::Option &&
                state.currentScreen == optionsui::Screen::Option,
            "Option tab activates Option projection before frame playback");

        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        optionsui::OptionPlayback option;
        require(option.sync(state, display::Screen2D::Options, playback) &&
                playback.commands().pendingCount() == 6 && option.visible(),
            "Option frame queues title/subtitles plus Start+Move+Stay OK atomically");
        require(playback.update(0).has_value(),
            "Option frame opening transition executes");
        bool allPresent = true;
        for (const auto tag : optionsui::OptionScreenTags)
            allPresent = allPresent && playback.runtime().matching(
                optionsui::optionSequence(tag), optionsui::OptionScreenPriority).size() == 1;
        require(allPresent,
            "Option title, Sound/Display subtitles and OK publish at priority 50");

        const auto okayId = optionsui::optionSequence(optionsui::OptionOkayInTag);
        const auto okayRoots = playback.runtime().matching(okayId, optionsui::OptionScreenPriority);
        const auto* okayObject = okayRoots.empty() ? nullptr : playback.world2D().find(okayRoots.front());
        require(okayObject && okayObject->worldTransform.values[6] == 350.0F &&
                okayObject->worldTransform.values[7] == 450.0F,
            "Option OK uses retail StartXY(350,450)");
        require(playback.update(100).has_value() &&
                playback.runtime().matching(okayId, optionsui::OptionScreenPriority).size() == 1,
            "Option OK incoming animation stays on its final frame");
        require(option.sync(state, display::Screen2D::Options, playback) &&
                playback.commands().pendingCount() == 0,
            "unchanged Option frame queues no redundant commands");

        const auto miss = optionsui::processInput(
            state, display::Screen2D::Options, click(349, 450));
        require(!miss.pressedOptionOkay && !miss.requestedBackdrop && state.active,
            "Option OK preserves Win32 left edge and ignores adjacent pixels");
        const auto okay = optionsui::processInput(
            state, display::Screen2D::Options, click(350, 450));
        require(okay.pressedOptionOkay &&
                okay.requestedBackdrop == display::Screen2D::Trade && !state.active,
            "Option OK returns to exact previous IBar view");
        require(option.sync(state, display::Screen2D::Options, playback) &&
                playback.commands().pendingCount() == 4 && playback.update(101).has_value() &&
                !option.visible(),
            "Option OK removes exactly four autonomous frame sequences");
    }

    void testOptionToggles()
    {
        require(optionsui::ToggleOffUnselectedTag == 0x026B &&
                optionsui::ToggleOffSelectedTag == 0x026C &&
                optionsui::ToggleOnUnselectedTag == 0x0272 &&
                optionsui::ToggleOnSelectedTag == 0x0273 &&
                optionsui::togglePriority(optionsui::OptionToggle::TokenAnimations) == 53 &&
                optionsui::togglePriority(optionsui::OptionToggle::Board3D) == 56,
            "Option toggles use retail shared CNKs and priority 50+i");
        const auto cameraOn = optionsui::optionToggleRect(
            optionsui::OptionToggle::Camera, true);
        const auto cameraOff = optionsui::optionToggleRect(
            optionsui::OptionToggle::Camera, false);
        require(cameraOn.left == 457 && cameraOn.top == 175 &&
                cameraOn.right == 516 && cameraOn.bottom == 208 &&
                cameraOff.left == 516 && cameraOff.right == 575,
            "Camera toggle preserves retail On/Off rectangles");
        require(optionsui::optionToggleSupported(optionsui::OptionToggle::TokenAnimations) &&
                optionsui::optionToggleSupported(optionsui::OptionToggle::Camera) &&
                optionsui::optionToggleSupported(optionsui::OptionToggle::Lighting) &&
                optionsui::optionToggleSupported(optionsui::OptionToggle::Board3D) &&
                !optionsui::optionToggleSupported(optionsui::OptionToggle::Music),
            "only toggles with real modern owners are interactive");

        optionsui::State state{};
        require(optionsui::beginFromIBar(state, display::Screen2D::Main),
            "toggle fixture enters Options from Main");
        const auto optionTab = optionsui::menuButtonRect(optionsui::MenuButton::Option);
        (void)optionsui::processInput(state, display::Screen2D::Options,
            click((optionTab.left + optionTab.right) / 2, optionTab.top + 1));
        optionsui::loadSupportedOptionValues(state, true, false, true, false);
        require(state.optionSnapshotLoaded &&
                state.optionOn[static_cast<std::size_t>(optionsui::OptionToggle::TokenAnimations)] &&
                !state.optionOn[static_cast<std::size_t>(optionsui::OptionToggle::Camera)] &&
                state.optionOn[static_cast<std::size_t>(optionsui::OptionToggle::Lighting)] &&
                !state.optionOn[static_cast<std::size_t>(optionsui::OptionToggle::Board3D)],
            "Option tab snapshots four supported runtime owners");

        const auto music = optionsui::optionToggleRect(optionsui::OptionToggle::Music, true);
        const auto ignored = optionsui::processInput(state, display::Screen2D::Options,
            click(music.left + 1, music.top + 1));
        require(!ignored.pressedOptionToggle,
            "unported Music control cannot pretend to change runtime state");

        const auto lighting = optionsui::optionToggleRect(optionsui::OptionToggle::Lighting, true);
        const auto lightClick = optionsui::processInput(state, display::Screen2D::Options,
            click(lighting.left + 1, lighting.top + 1));
        require(lightClick.pressedOptionToggle == optionsui::OptionToggle::Lighting &&
                !state.optionOn[static_cast<std::size_t>(optionsui::OptionToggle::Lighting)],
            "supported toggle click flips temporary value without applying early");

        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        optionsui::TogglePlayback toggles;
        require(toggles.sync(state, display::Screen2D::Options, playback) &&
                playback.commands().pendingCount() == 24,
            "four supported toggles open as two Start+Move+Stay roots each");
        require(playback.update(0).has_value() && playback.world2D().size() == 8,
            "supported Option toggles publish eight Overlay2D roots");

        const auto cameraOnId = optionsui::toggleSequence(true, false);
        const auto cameraOffId = optionsui::toggleSequence(false, false);
        const auto cameraOnRoots = playback.runtime().matching(cameraOnId, 54);
        const auto cameraOffRoots = playback.runtime().matching(cameraOffId, 54);
        const auto* cameraOnObject = cameraOnRoots.empty() ? nullptr :
            playback.world2D().find(cameraOnRoots.front());
        const auto* cameraOffObject = cameraOffRoots.empty() ? nullptr :
            playback.world2D().find(cameraOffRoots.front());
        require(cameraOnObject && cameraOffObject &&
                cameraOnObject->worldTransform.values[6] == 457.0F &&
                cameraOnObject->worldTransform.values[7] == 175.0F &&
                cameraOffObject->worldTransform.values[6] == 516.0F &&
                cameraOffObject->worldTransform.values[7] == 175.0F,
            "Camera Off renders unselected On and selected Off at retail coordinates");

        const auto cameraClick = optionsui::processInput(state, display::Screen2D::Options,
            click(cameraOff.left + 1, cameraOff.top + 1));
        require(cameraClick.pressedOptionToggle == optionsui::OptionToggle::Camera &&
                toggles.sync(state, display::Screen2D::Options, playback) &&
                playback.commands().pendingCount() == 8,
            "toggle change queues Stop pair plus replacement Start+Move+Stay pair");
        require(playback.update(1).has_value() &&
                toggles.shown(optionsui::OptionToggle::Camera) == 1 &&
                playback.runtime().matching(optionsui::toggleSequence(true, true), 54).size() == 1 &&
                playback.runtime().matching(optionsui::toggleSequence(false, true), 54).size() == 1,
            "Camera visual swaps atomically to selected On state");

        const auto creditsTab = optionsui::menuButtonRect(optionsui::MenuButton::Credits);
        (void)optionsui::processInput(state, display::Screen2D::Options,
            click(creditsTab.left + 1, creditsTab.top + 1));
        require(!state.optionSnapshotLoaded &&
                toggles.sync(state, display::Screen2D::Options, playback) &&
                playback.commands().pendingCount() == 8 && playback.update(2).has_value() &&
                playback.world2D().size() == 0,
            "leaving Option tab discards snapshot and stops eight supported toggle roots");
    }

    void testHelpScreen()
    {
        require(optionsui::HelpTitleTag == 0x0249 &&
                optionsui::HelpButtonInTags == std::array<data::DataTag, 3>{
                    0x0250, 0x024B, 0x0235} &&
                optionsui::HelpScreenPriority == 50 &&
                optionsui::HelpScreenStayAtEnd == 2,
            "Help screen uses retail title/button CNKs, priority 50 and StayAtEnd");

        const std::array<int, 3> tops{180, 288, 401};
        bool exact = true;
        for (std::size_t index = 0; index < tops.size(); ++index)
        {
            const auto rect = optionsui::helpButtonRect(
                static_cast<optionsui::HelpButton>(index));
            exact = exact && rect.left == 291 && rect.top == tops[index] &&
                rect.right == 511 && rect.bottom == tops[index] + 62;
        }
        require(exact &&
                optionsui::helpButtonHit(291, 180) == optionsui::HelpButton::QuickHelp &&
                optionsui::helpButtonHit(291, 288) == optionsui::HelpButton::FullHelp &&
                optionsui::helpButtonHit(291, 401) == optionsui::HelpButton::Cancel &&
                !optionsui::helpButtonHit(511, 401),
            "Help hotspots match retail Quick/Full/Cancel rectangles");

        optionsui::State state{};
        require(optionsui::beginFromIBar(state, display::Screen2D::Portfolio),
            "Help fixture enters Options from Portfolio");
        const auto helpTab = optionsui::menuButtonRect(optionsui::MenuButton::Help);
        const auto tab = optionsui::processInput(state, display::Screen2D::Options,
            click((helpTab.left + helpTab.right) / 2, helpTab.top + 1));
        require(tab.pressedMenuButton == optionsui::MenuButton::Help &&
                state.currentScreen == optionsui::Screen::Help,
            "Help navigation activates retail Help screen");

        const auto quick = optionsui::processInput(
            state, display::Screen2D::Options, click(300, 190));
        const auto full = optionsui::processInput(
            state, display::Screen2D::Options, click(300, 300));
        require(quick.pressedHelpButton == optionsui::HelpButton::QuickHelp &&
                full.pressedHelpButton == optionsui::HelpButton::FullHelp &&
                !quick.requestedBackdrop && !full.requestedBackdrop && state.active,
            "Quick/Full Help hits are identified without inventing WinHelp/file backends");

        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        optionsui::HelpPlayback help;
        require(help.sync(state, display::Screen2D::Options, playback) &&
                playback.commands().pendingCount() == 7 && help.visible(),
            "Help frame queues title plus three Start+Stay button sequences atomically");
        require(playback.update(0).has_value() && playback.world2D().size() == 4,
            "Help title and three incoming buttons publish exactly four roots");
        require(playback.update(100).has_value() && playback.world2D().size() == 4,
            "Help incoming button sequences stay on their final frame");
        require(help.sync(state, display::Screen2D::Options, playback) &&
                playback.commands().pendingCount() == 0,
            "unchanged Help frame queues no redundant commands");

        const auto cancel = optionsui::processInput(
            state, display::Screen2D::Options, click(300, 420));
        require(cancel.pressedHelpButton == optionsui::HelpButton::Cancel &&
                cancel.requestedBackdrop == display::Screen2D::Portfolio && !state.active,
            "Help Cancel returns to exact previous IBar view");
        require(help.sync(state, display::Screen2D::Options, playback) &&
                playback.commands().pendingCount() == 4 && playback.update(101).has_value() &&
                !help.visible() && playback.world2D().size() == 0,
            "Help Cancel removes exactly the four autonomous Help sequences");
    }

    void testTransactionalFailures()
    {
        optionsui::State state{};
        require(optionsui::beginFromIBar(state, display::Screen2D::Main),
            "failure fixture enters Options");
        engine::SequencePlayback missing(nullptr);
        optionsui::FilePlayback missingFile;
        const auto missingResult = missingFile.sync(
            state, display::Screen2D::Options, missing);
        require(!missingResult && missing.commands().pendingCount() == 0 &&
                !missingFile.visible(),
            "missing File-screen resources reject before queue mutation");

        SyntheticSequenceResources resources;
        engine::SequencePlayback full(resources.service.snapshot());
        optionsui::FilePlayback fullFile;
        bool filled = true;
        for (std::size_t index = 0;
             index < sequence::SequenceCommandQueue::Capacity - 11; ++index)
        {
            const auto queued = full.commands().enqueue(
                sequence::StopSequenceCommand{data::EmptyDataId, 0, false});
            filled = filled && queued.has_value();
        }
        require(filled,
            "FIFO fixture leaves only eleven slots for twelve-command File open");
        const auto before = full.commands().pendingCount();
        const auto noRoom = fullFile.sync(
            state, display::Screen2D::Options, full);
        require(!noRoom && full.commands().pendingCount() == before &&
                !fullFile.visible(),
            "insufficient FIFO preserves hidden File-screen state transactionally");


        engine::SequencePlayback missingNavPlayback(nullptr);
        optionsui::NavigationPlayback missingNav;
        const auto missingNavResult = missingNav.sync(
            state, display::Screen2D::Options, missingNavPlayback);
        require(!missingNavResult && missingNavPlayback.commands().pendingCount() == 0 &&
                missingNav.visual(optionsui::MenuButton::File) == optionsui::NavigationVisual::Off,
            "missing navigation resources reject before queue mutation");

        SyntheticSequenceResources navResources;
        engine::SequencePlayback navPlayback(navResources.service.snapshot());
        optionsui::NavigationPlayback nav;
        require(nav.sync(state, display::Screen2D::Options, navPlayback) &&
                navPlayback.update(0).has_value(),
            "navigation FIFO fixture opens initial File state");
        const auto optionRect = optionsui::menuButtonRect(optionsui::MenuButton::Option);
        (void)optionsui::processInput(state, display::Screen2D::Options,
            click((optionRect.left + optionRect.right) / 2, optionRect.top + 1));
        bool navFilled = true;
        for (std::size_t index = 0;
             index < sequence::SequenceCommandQueue::Capacity - 5; ++index)
        {
            const auto queued = navPlayback.commands().enqueue(
                sequence::StopSequenceCommand{data::EmptyDataId, 0, false});
            navFilled = navFilled && queued.has_value();
        }
        require(navFilled,
            "navigation FIFO fixture leaves five slots for six-command tab switch");
        const auto navBefore = navPlayback.commands().pendingCount();
        const auto navNoRoom = nav.sync(state, display::Screen2D::Options, navPlayback);
        require(!navNoRoom && navPlayback.commands().pendingCount() == navBefore &&
                nav.visual(optionsui::MenuButton::File) == optionsui::NavigationVisual::Press &&
                nav.visual(optionsui::MenuButton::Option) == optionsui::NavigationVisual::Idle,
            "insufficient FIFO preserves complete Options navigation state transactionally");


        optionsui::State optionState{};
        require(optionsui::beginFromIBar(optionState, display::Screen2D::Main),
            "Option failure fixture enters Options");
        const auto optionTabRect = optionsui::menuButtonRect(optionsui::MenuButton::Option);
        (void)optionsui::processInput(optionState, display::Screen2D::Options,
            click((optionTabRect.left + optionTabRect.right) / 2, optionTabRect.top + 1));
        engine::SequencePlayback missingOptionPlayback(nullptr);
        optionsui::OptionPlayback missingOption;
        const auto missingOptionResult = missingOption.sync(
            optionState, display::Screen2D::Options, missingOptionPlayback);
        require(!missingOptionResult && missingOptionPlayback.commands().pendingCount() == 0 &&
                !missingOption.visible(),
            "missing Option-frame resources reject before queue mutation");

        SyntheticSequenceResources optionResources;
        engine::SequencePlayback optionPlayback(optionResources.service.snapshot());
        optionsui::OptionPlayback option;
        bool optionFilled = true;
        for (std::size_t index = 0;
             index < sequence::SequenceCommandQueue::Capacity - 5; ++index)
        {
            const auto queued = optionPlayback.commands().enqueue(
                sequence::StopSequenceCommand{data::EmptyDataId, 0, false});
            optionFilled = optionFilled && queued.has_value();
        }
        require(optionFilled,
            "Option FIFO fixture leaves five slots for six-command frame open");
        const auto optionBefore = optionPlayback.commands().pendingCount();
        const auto optionNoRoom = option.sync(
            optionState, display::Screen2D::Options, optionPlayback);
        require(!optionNoRoom && optionPlayback.commands().pendingCount() == optionBefore &&
                !option.visible(),
            "insufficient FIFO preserves hidden Option-frame state transactionally");


        optionsui::State toggleState{};
        require(optionsui::beginFromIBar(toggleState, display::Screen2D::Main),
            "toggle failure fixture enters Options");
        (void)optionsui::processInput(toggleState, display::Screen2D::Options,
            click((optionTabRect.left + optionTabRect.right) / 2, optionTabRect.top + 1));
        optionsui::loadSupportedOptionValues(toggleState, true, true, true, true);
        engine::SequencePlayback missingTogglePlayback(nullptr);
        optionsui::TogglePlayback missingToggle;
        const auto missingToggleResult = missingToggle.sync(
            toggleState, display::Screen2D::Options, missingTogglePlayback);
        require(!missingToggleResult && missingTogglePlayback.commands().pendingCount() == 0 &&
                missingToggle.shown(optionsui::OptionToggle::Camera) == -1,
            "missing toggle resources reject before queue mutation");

        SyntheticSequenceResources toggleResources;
        engine::SequencePlayback togglePlayback(toggleResources.service.snapshot());
        optionsui::TogglePlayback toggleVisual;
        bool toggleFilled = true;
        for (std::size_t index = 0;
             index < sequence::SequenceCommandQueue::Capacity - 23; ++index)
        {
            const auto queued = togglePlayback.commands().enqueue(
                sequence::StopSequenceCommand{data::EmptyDataId, 0, false});
            toggleFilled = toggleFilled && queued.has_value();
        }
        require(toggleFilled,
            "toggle FIFO fixture leaves twenty-three slots for twenty-four-command open");
        const auto toggleBefore = togglePlayback.commands().pendingCount();
        const auto toggleNoRoom = toggleVisual.sync(
            toggleState, display::Screen2D::Options, togglePlayback);
        require(!toggleNoRoom && togglePlayback.commands().pendingCount() == toggleBefore &&
                toggleVisual.shown(optionsui::OptionToggle::Lighting) == -1,
            "insufficient FIFO preserves hidden supported-toggle state transactionally");

        optionsui::State helpState{};
        require(optionsui::beginFromIBar(helpState, display::Screen2D::Main),
            "Help failure fixture enters Options");
        const auto helpTabRect = optionsui::menuButtonRect(optionsui::MenuButton::Help);
        (void)optionsui::processInput(helpState, display::Screen2D::Options,
            click((helpTabRect.left + helpTabRect.right) / 2, helpTabRect.top + 1));
        engine::SequencePlayback missingHelpPlayback(nullptr);
        optionsui::HelpPlayback missingHelp;
        const auto missingHelpResult = missingHelp.sync(
            helpState, display::Screen2D::Options, missingHelpPlayback);
        require(!missingHelpResult && missingHelpPlayback.commands().pendingCount() == 0 &&
                !missingHelp.visible(),
            "missing Help resources reject before queue mutation");

        SyntheticSequenceResources helpResources;
        engine::SequencePlayback helpPlayback(helpResources.service.snapshot());
        optionsui::HelpPlayback helpVisual;
        bool helpFilled = true;
        for (std::size_t index = 0;
             index < sequence::SequenceCommandQueue::Capacity - 6; ++index)
        {
            const auto queued = helpPlayback.commands().enqueue(
                sequence::StopSequenceCommand{data::EmptyDataId, 0, false});
            helpFilled = helpFilled && queued.has_value();
        }
        require(helpFilled,
            "Help FIFO fixture leaves six slots for seven-command open");
        const auto helpBefore = helpPlayback.commands().pendingCount();
        const auto helpNoRoom = helpVisual.sync(
            helpState, display::Screen2D::Options, helpPlayback);
        require(!helpNoRoom && helpPlayback.commands().pendingCount() == helpBefore &&
                !helpVisual.visible(),
            "insufficient FIFO preserves hidden Help-screen state transactionally");
    }
}

int main()
{
    try
    {
        testRetailContract();
        testFileInput();
        testFilePlayback();
        testNavigationInputAndPlayback();
        testOptionScreenFrame();
        testOptionToggles();
        testHelpScreen();
        testTransactionalFailures();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
