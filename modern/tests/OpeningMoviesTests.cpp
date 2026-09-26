#include "OpeningMovies.hpp"
#include "SequencePlayback.hpp"
#include "SequenceVideoRuntime.hpp"
#include "SyntheticSequenceResources.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    using namespace monopoly;

    void require(bool value, std::string_view text)
    {
        if (!value) throw std::runtime_error(std::string(text));
    }

    struct Fixture : SyntheticSequenceResources
    {
        Fixture()
        {
            using namespace data;
            service.shutdown();
            std::vector<ArchiveBuildItem> items(5);
            items[4] = {LegacyDataType::Uap, uap8()};
            const auto written = writeLegacyDataArchive(directory / "Dat_Mon/dat_lm01.dat", items);
            require(written.has_value(), "write explicit synthetic trademark UAP");
            const auto paths = ResourcePaths::create(std::array{directory});
            require(paths.has_value() && service.initialize(*paths).has_value(),
                "initialize real resource registry containing synthetic trademark");
        }

        void file(std::string_view relative)
        {
            const auto path = directory / relative;
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream(path, std::ios::binary);
            // Path selection fixture only. Decoder output/EOF is an explicit
            // boundary input in these controller tests, never a decode claim.
            stream << "opening-controller path fixture";
            require(static_cast<bool>(stream), "write movie path selection fixture");
        }

        void allAvi()
        {
            file("AVI/HLogo.avi");
            file("AVI/ALogo.avi");
            file("AVI/MIntro.avi");
        }
    };

    void assertVideo(engine::SequencePlayback& playback,
        const openingmovies::Controller& controller, std::string_view file, bool bink)
    {
        const auto instances = playback.runtime().videoInstances();
        require(instances.size() == 1 && instances[0].node == controller.movieNode(),
            "controller owns a real runtime video node");
        const auto& video = instances[0];
        require(video.fileName == file && video.priority == 1100 && video.endingAction == 1,
            "runtime video preserves selected filename, priority and stop action");
        require(video.video.enableAudio && video.video.enableVideo && video.video.drawSolid &&
            video.video.alphaLevel == 255 && video.video.drawDirectlyToScreen,
            "opening video requests opaque presentation with audio");
        require(video.binkDoubleSize == bink,
            "only Bink intro requests runtime double-intrinsic-size centering");
        require(video.boundingBox && video.boundingBox->left == (bink ? 0 : 200) &&
            video.boundingBox->top == (bink ? 0 : 150) &&
            video.boundingBox->right == (bink ? 800 : 600) &&
            video.boundingBox->bottom == (bink ? 600 : 450),
            "Bink viewport cap and explicit 32-bit AVI destination reach the bridge");
    }

    void testTimelineAndEof()
    {
        Fixture fixture;
        fixture.allAvi();
        engine::SequencePlayback playback(fixture.service.snapshot());
        openingmovies::Controller controller;
        controller.begin(100, false, true, playback);
        require(controller.phase() == openingmovies::Phase::Trademark && !controller.pointerVisible(),
            "opening trademark hides pointer");
        const auto trademark = playback.runtime().roots();
        require(trademark.size() == 1 && playback.runtime().inspect(trademark[0])->priority == 0 &&
            playback.runtime().inspect(trademark[0])->dataId ==
                data::packDataId(data::LegacyGroupId::LanguageGraphics, 4),
            "actual localized trademark starts at priority zero");
        controller.update(99, {}, playback);
        controller.update(640, {}, playback);
        require(controller.phase() == openingmovies::Phase::Trademark,
            "no tick underflow and strict 540-tick trademark boundary");
        controller.update(641, {}, playback);
        require(controller.phase() == openingmovies::Phase::Movies && playback.runtime().roots().empty(),
            "trademark stops after nine seconds; movie begins on following event");
        controller.update(642, {}, playback);
        assertVideo(playback, controller, "AVI/HLogo.avi", false);
        // Advance the production command owner as well as its runtime. A
        // direct runtime.update would leave ExecuteCommands' clock stale when
        // the controller starts the next movie in this same playback session.
        require(playback.update(642).has_value(), "runtime video timeline is executable");
        const auto initialVideo = playback.runtime().videoInstances()[0];
        require(playback.update(1242).has_value(), "video parent timeline advances");
        const auto waitingVideo = playback.runtime().videoInstances()[0];
        require(initialVideo.clock == 0 && waitingVideo.clock == 0 &&
            waitingVideo.elapsedParentClock - initialVideo.elapsedParentClock == 600,
            "video media waits for decoder input while its independent parent clock advances");
        controller.update(1'000'000, {}, playback);
        require(controller.movieFile() == "AVI/HLogo.avi",
            "elapsed time does not simulate decoder EOF");
        std::array<video::SequencePlaybackState, 1> states{};
        states[0].node = controller.movieNode() + 999;
        states[0].status.ended = true;
        controller.update(1'000'001, states, playback);
        require(controller.movieFile() == "AVI/HLogo.avi", "unrelated video EOF is ignored");
        for (const auto next : {"AVI/ALogo.avi", "AVI/MIntro.avi"})
        {
            states[0].node = controller.movieNode();
            const auto previous = controller.movieNode();
            controller.update(1'000'002, states, playback);
            require(!playback.runtime().inspect(previous), "completed video node is released");
            assertVideo(playback, controller, next, false);
        }
        states[0].node = controller.movieNode();
        // The backend's Stop lifecycle can remove the node before the owner
        // consumes its published real EOF on the next engine frame.
        (void)playback.runtime().stop(controller.movieNode());
        controller.update(1'000'003, states, playback);
        require(!controller.active() && controller.pointerVisible() &&
            controller.takePlayerSelectionRequest() && !controller.takePlayerSelectionRequest() &&
            playback.runtime().roots().empty(),
            "third real EOF restores pointer and requests HiScore exactly once");
    }

    void testSkipLobbyAndSelection()
    {
        Fixture fixture;
        fixture.allAvi();
        fixture.file("hlogo.BIK");
        engine::SequencePlayback playback(fixture.service.snapshot());
        openingmovies::Controller controller;
        controller.begin(0, false, true, playback);
        controller.skip(playback);
        require(controller.active() && !controller.takePlayerSelectionRequest(),
            "trademark key/click skips only the trademark");
        controller.update(1, {}, playback);
        assertVideo(playback, controller, "HLogo.bik", true);
        controller.skip(playback);
        require(!controller.active() && controller.takePlayerSelectionRequest(),
            "movie key/click skips all remaining opening movies");
        controller.begin(2, false, false, playback);
        controller.skip(playback);
        controller.update(3, {}, playback);
        assertVideo(playback, controller, "AVI/HLogo.avi", false);
        controller.reset(playback);
        require(!controller.active() && !controller.takePlayerSelectionRequest() &&
            playback.runtime().roots().empty(), "reset releases owned movie without requesting a screen");
        engine::SequencePlayback noResources(nullptr);
        controller.begin(4, true, true, noResources);
        require(controller.takePlayerSelectionRequest() && controller.takeError().empty() &&
            noResources.runtime().roots().empty(), "lobby bypass never loads trademark or video assets");
    }

    void testFailuresAndOwnership()
    {
        Fixture fixture;
        engine::SequencePlayback playback(fixture.service.snapshot());
        auto unrelated = sequence::SequenceProgram::rawBitmap(
            0xFFFE1234, data::LegacyDataType::Native);
        require(unrelated.has_value(), "independent UI fixture program");
        const auto other = playback.runtime().start(*unrelated, 1200);
        require(other.has_value(), "independent UI node exists");
        openingmovies::Controller controller;
        controller.begin(0, false, true, playback);
        controller.skip(playback);
        controller.update(1, {}, playback);
        require(!controller.active() && controller.takePlayerSelectionRequest() &&
            !controller.takeError().empty() && playback.runtime().inspect(*other),
            "missing movie exits intro without destroying unrelated UI nodes");
        fixture.allAvi();
        fixture.file("HLogo.bik");
        controller.begin(2, false, true, playback);
        controller.skip(playback);
        controller.update(3, {}, playback);
        controller.decoderFailed("explicit decoder failure", playback);
        require(!controller.active() && controller.takePlayerSelectionRequest() &&
            controller.takeError() == "explicit decoder failure" && controller.takeError().empty() &&
            playback.runtime().videoInstances().empty(),
            "Bink decode failure ends intro without falling back to AVI or inventing EOF");
        controller.begin(4, false, false, playback);
        controller.skip(playback);
        controller.update(5, {}, playback);
        (void)playback.runtime().stop(controller.movieNode());
        controller.update(6, {}, playback);
        require(!controller.active() && !controller.takeError().empty(),
            "lost runtime node exits instead of hanging intro");
        engine::SequencePlayback noResources(nullptr);
        controller.begin(7, false, true, noResources);
        require(controller.takePlayerSelectionRequest() && !controller.takeError().empty(),
            "missing trademark resource exits intro explicitly");
    }

    void testFactoryValidation()
    {
        data::Sequence2DBoundingBoxAttribute bounds{{}, 0, 0, 800, 600};
        require(!sequence::SequenceProgram::runtimeVideo(0, "HLogo.avi", {}, bounds),
            "runtime video rejects empty identity");
        require(!sequence::SequenceProgram::runtimeVideo(1, "", {}, bounds),
            "runtime video rejects empty filename");
        require(!sequence::SequenceProgram::runtimeVideo(1, std::string("bad\0name", 8), {}, bounds),
            "runtime video rejects embedded filename terminator");
        bounds.right = 0;
        require(!sequence::SequenceProgram::runtimeVideo(1, "HLogo.avi", {}, bounds),
            "runtime video rejects empty destination rectangle");
    }
}

int main()
{
    try
    {
        testTimelineAndEof();
        testSkipLobbyAndSelection();
        testFailuresAndOwnership();
        testFactoryValidation();
        std::cout << "[PASS] opening movies: 4 controller/runtime contract groups\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] opening movies: " << error.what() << '\n';
        return 1;
    }
}
