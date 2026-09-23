#include "VideoRuntime.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace monopoly;

namespace
{
    void require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
        std::cout << "[PASS] " << message << '\n';
    }

    void appendU32(
        std::vector<std::uint8_t>& bytes,
        std::uint32_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
        bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
        bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
    }

    void appendFourcc(
        std::vector<std::uint8_t>& bytes,
        const char (&id)[5])
    {
        bytes.insert(bytes.end(), id, id + 4);
    }

    std::vector<std::uint8_t> aviFixture()
    {
        std::vector<std::uint8_t> avih(56, 0);
        auto set = [&](std::size_t offset, std::uint32_t value)
        {
            avih[offset] = static_cast<std::uint8_t>(value);
            avih[offset + 1] =
                static_cast<std::uint8_t>(value >> 8U);
            avih[offset + 2] =
                static_cast<std::uint8_t>(value >> 16U);
            avih[offset + 3] =
                static_cast<std::uint8_t>(value >> 24U);
        };
        set(0, 33333);
        set(16, 10);
        set(32, 320);
        set(36, 200);

        std::vector<std::uint8_t> list;
        appendFourcc(list, "hdrl");
        appendFourcc(list, "avih");
        appendU32(list, static_cast<std::uint32_t>(avih.size()));
        list.insert(list.end(), avih.begin(), avih.end());

        std::vector<std::uint8_t> bytes;
        appendFourcc(bytes, "RIFF");
        appendU32(bytes, static_cast<std::uint32_t>(
            4 + 8 + list.size()));
        appendFourcc(bytes, "AVI ");
        appendFourcc(bytes, "LIST");
        appendU32(bytes, static_cast<std::uint32_t>(list.size()));
        bytes.insert(bytes.end(), list.begin(), list.end());
        return bytes;
    }

    void testMetadataAndTimeline()
    {
        const auto bytes = aviFixture();
        const auto metadata = video::parseAviMetadata(bytes);
        require(metadata.has_value(),
            "portable AVI parser reads RIFF main header");
        require(metadata->microsecondsPerFrame == 33333 &&
                metadata->totalFrames == 10 &&
                metadata->width == 320 &&
                metadata->height == 200,
            "AVI metadata matches legacy avih fields");

        video::Runtime runtime;
        require(runtime.open(bytes).has_value(),
            "video runtime opens validated AVI metadata");
        const auto frame = runtime.frameAtElapsed(100000);
        require(frame && *frame == 3,
            "elapsed time maps to the expected video frame");

        const auto fed = runtime.feedToFrame(3);
        require(fed && fed->currentFrame == 3 &&
                fed->desiredFrame == 3 && !fed->ended,
            "silent-film style feed advances to requested frame");

        require(runtime.cutToFrame(1).has_value() &&
                runtime.status().currentFrame == 1,
            "CutVideoToFrame contract updates current frame");

        require(runtime.setAlternative(3, 8).has_value() &&
                runtime.changeAlternative(3, true),
            "video alternative jump can be armed");
        const auto jumped = runtime.feedToFrame(5);
        require(jumped && jumped->currentFrame == 8 &&
                jumped->desiredFrame == 8,
            "armed alternative jumps when decision frame is crossed");

        runtime.forgetAlternatives();
        require(runtime.cutToFrame(0).has_value(),
            "video returns to start after jump test");
        const auto straight = runtime.feedToFrame(5);
        require(straight && straight->currentFrame == 5,
            "forget alternatives restores straight playback");

        const auto ended = runtime.feedToFrame(20);
        require(ended && ended->ended &&
                ended->currentFrame == 10,
            "non-looping playback pauses at legacy end sentinel");
    }

    void testLoopAndValidation()
    {
        const auto bytes = aviFixture();
        video::Runtime loop;
        require(loop.open(bytes, true).has_value(),
            "looping runtime opens the same AVI");
        const auto wrapped = loop.feedToFrame(12);
        require(wrapped && wrapped->currentFrame == 2 &&
                !wrapped->ended,
            "loop-at-end wraps requested frame modulo frame count");

        auto corrupt = bytes;
        corrupt[0] = 'N';
        require(!video::parseAviMetadata(corrupt),
            "non-RIFF bytes are rejected");
        require(!loop.cutToFrame(99),
            "out-of-range cut target is rejected");
        require(!loop.setAlternative(2, 99),
            "out-of-range alternative jump is rejected");

        loop.stop();
        require(!loop.open() && !loop.feedToFrame(0),
            "StopVideo contract clears the runtime session");
    }
}

int main()
{
    try
    {
        testMetadataAndTimeline();
        testLoopAndValidation();
        std::cout << "Video runtime contract tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
