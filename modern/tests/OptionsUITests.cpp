#include "OptionsFilePlayback.hpp"
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
    }
}

int main()
{
    try
    {
        testRetailContract();
        testFileInput();
        testFilePlayback();
        testTransactionalFailures();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
