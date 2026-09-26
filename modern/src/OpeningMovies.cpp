#include "OpeningMovies.hpp"

#include "SequencePlayback.hpp"
#include "SequenceVideoRuntime.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace monopoly::openingmovies
{
    namespace
    {
        constexpr auto TrademarkId = data::packDataId(
            data::LegacyGroupId::LanguageGraphics, 0x0004);
        constexpr std::uint16_t MoviePriority = 1100;
        constexpr std::array MovieNames{"HLogo", "ALogo", "MIntro"};

        std::optional<std::string> resolveMovie(const data::ResourcePaths& paths,
            std::string_view base, bool use3DBoard)
        {
            const auto find = [&](std::string_view extension) -> std::optional<std::string> {
                const std::string name = std::string(base) + std::string(extension);
                if (paths.resolve(name)) return name;
                const std::string inAviDirectory = "AVI/" + name;
                if (paths.resolve(inAviDirectory)) return inAviDirectory;
                return std::nullopt;
            };
            if (use3DBoard)
                if (auto bink = find(".bik")) return bink;
            return find(".avi");
        }
    }

    void Controller::stopOwnedNodes(engine::SequencePlayback& playback)
    {
        if (trademark_ && playback.runtime().inspect(trademark_))
            (void)playback.runtime().stop(trademark_);
        if (movie_ && playback.runtime().inspect(movie_))
            (void)playback.runtime().stop(movie_);
        if (movieDataId_ != data::EmptyDataId)
            (void)playback.freeRuntimeSequence(movieDataId_);
        trademark_ = movie_ = 0;
        movieDataId_ = data::EmptyDataId;
    }

    void Controller::reset(engine::SequencePlayback& playback)
    {
        stopOwnedNodes(playback);
        phase_ = Phase::Idle;
        nextMovie_ = 0;
        selectionRequested_ = false;
        movieFile_.clear();
        error_.clear();
    }

    void Controller::finish(engine::SequencePlayback& playback)
    {
        stopOwnedNodes(playback);
        phase_ = Phase::Finished;
        selectionRequested_ = true;
        movieFile_.clear();
    }

    void Controller::decoderFailed(std::string error, engine::SequencePlayback& playback)
    {
        if (!active()) return;
        error_ = std::move(error);
        finish(playback);
    }

    void Controller::begin(std::uint64_t tick, bool startedByLobby, bool use3DBoard,
        engine::SequencePlayback& playback)
    {
        reset(playback);
        use3DBoard_ = use3DBoard;
        if (startedByLobby)
        {
            finish(playback);
            return;
        }
        phase_ = Phase::Trademark;
        trademarkStarted_ = tick;
        auto program = sequence::SequenceProgram::load(playback.resources(), TrademarkId);
        if (!program)
        {
            decoderFailed("opening trademark: " + program.error().detail, playback);
            return;
        }
        auto node = playback.runtime().start(*program, 0);
        if (!node)
        {
            decoderFailed("opening trademark: " + node.error().detail, playback);
            return;
        }
        trademark_ = *node;
    }

    void Controller::startNextMovie(engine::SequencePlayback& playback)
    {
        if (nextMovie_ == MovieNames.size())
        {
            finish(playback);
            return;
        }
        const auto resources = playback.resources();
        auto file = resources
            ? resolveMovie(resources->paths(), MovieNames[nextMovie_], use3DBoard_)
            : std::nullopt;
        if (!file)
        {
            decoderFailed(std::string("opening movie cannot be found: ") +
                MovieNames[nextMovie_], playback);
            return;
        }
        const bool bink = file->ends_with(".bik");
        // Retail's AVI path uses the central 400x300 rectangle at 32-bit depth.
        // Bink requests double intrinsic size, centered and clamped within the
        // 800x600 viewport. The bridge computes its real destination from the
        // decoded metadata before publishing the first frame.
        data::Sequence2DBoundingBoxAttribute bounds{};
        bounds.left = bink ? 0 : 200;
        bounds.top = bink ? 0 : 150;
        bounds.right = bink ? 800 : 600;
        bounds.bottom = bink ? 600 : 450;
        data::SequenceVideoData options{};
        options.drawSolid = true;
        options.alphaLevel = 255;
        options.enableVideo = options.enableAudio = true;
        options.drawDirectlyToScreen = true;
        auto runtimeId = playback.createVideoObject(
            *file, options, bounds, bink);
        if (!runtimeId)
        {
            decoderFailed(runtimeId.error(), playback);
            return;
        }
        const auto collecting = playback.collectCommands();
        if (!collecting)
        {
            (void)playback.freeRuntimeSequence(*runtimeId);
            decoderFailed(collecting.error(), playback);
            return;
        }

        const auto started = playback.start(*runtimeId, MoviePriority);
        const auto ending = started
            ? playback.setEndingAction(*runtimeId, MoviePriority, 1)
            : std::expected<void, std::string>{
                std::unexpected(started.error())};
        const auto executed = ending
            ? playback.executeCommands()
            : std::expected<int, std::string>{
                std::unexpected(ending.error())};

        if (!executed)
        {
            while (playback.commands().nestingLevel() > 0)
                (void)playback.executeCommands();
            (void)playback.freeRuntimeSequence(*runtimeId);
            decoderFailed(executed.error(), playback);
            return;
        }

        const auto nodes = playback.runtime().matching(
            *runtimeId, MoviePriority, false);
        if (nodes.empty())
        {
            (void)playback.freeRuntimeSequence(*runtimeId);
            decoderFailed(
                "opening movie start command produced no runtime node",
                playback);
            return;
        }

        movieDataId_ = *runtimeId;
        movie_ = nodes.front();
        movieFile_ = std::move(*file);
        ++nextMovie_;
    }

    void Controller::update(std::uint64_t tick,
        std::span<const video::SequencePlaybackState> videoStates,
        engine::SequencePlayback& playback)
    {
        if (phase_ == Phase::Trademark)
        {
            // Strict inequality matches screenStartTime + 60 * 9 < tick.
            if (tick >= trademarkStarted_ && tick - trademarkStarted_ > 540)
                skip(playback);
            return;
        }
        if (phase_ != Phase::Movies) return;
        if (movie_)
        {
            const auto state = std::find_if(videoStates.begin(), videoStates.end(),
                [&](const auto& value) { return value.node == movie_; });
            const bool ended = state != videoStates.end() && state->status.ended;
            const bool live = playback.runtime().inspect(movie_).has_value();
            // A decoded Stop EOF can already have removed its runtime node in
            // the preceding update. Its published EOF is still authoritative.
            if (!ended && !live)
            {
                decoderFailed("opening movie sequence disappeared before decoded EOF", playback);
                return;
            }
            if (!ended) return;
            if (live) (void)playback.runtime().stop(movie_);
            if (movieDataId_ != data::EmptyDataId)
                (void)playback.freeRuntimeSequence(movieDataId_);
            movie_ = 0;
            movieDataId_ = data::EmptyDataId;
        }
        startNextMovie(playback);
    }

    void Controller::skip(engine::SequencePlayback& playback)
    {
        if (phase_ == Phase::Trademark)
        {
            stopOwnedNodes(playback);
            phase_ = Phase::Movies;
        }
        else if (phase_ == Phase::Movies) finish(playback);
    }

    bool Controller::takePlayerSelectionRequest() noexcept
    { return std::exchange(selectionRequested_, false); }

    std::string Controller::takeError() noexcept
    { return std::exchange(error_, {}); }
}
