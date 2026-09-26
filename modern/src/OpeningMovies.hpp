#pragma once

#include "SequenceRuntime.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace monopoly::engine { class SequencePlayback; }
namespace monopoly::video { struct SequencePlaybackState; }

namespace monopoly::openingmovies
{
    enum class Phase { Idle, Trademark, Movies, Finished };

    // Owner-thread opening sequence from Userifce.cpp. This owns only its own
    // runtime nodes. Engine applies Black/PlayerSelect and pointer visibility;
    // the video bridge owns actual decoding, audio, presentation and EOF.
    class Controller final
    {
    public:
        void begin(std::uint64_t tick, bool startedByLobby, bool use3DBoard,
            engine::SequencePlayback& playback);
        void update(std::uint64_t tick,
            std::span<const video::SequencePlaybackState> videoStates,
            engine::SequencePlayback& playback);
        // Call for any key press or left/middle/right button down, never motion.
        void skip(engine::SequencePlayback& playback);
        void decoderFailed(std::string error, engine::SequencePlayback& playback);
        void reset(engine::SequencePlayback& playback);

        [[nodiscard]] bool active() const noexcept
        { return phase_ == Phase::Trademark || phase_ == Phase::Movies; }
        [[nodiscard]] bool pointerVisible() const noexcept { return !active(); }
        [[nodiscard]] Phase phase() const noexcept { return phase_; }
        [[nodiscard]] bool takePlayerSelectionRequest() noexcept;
        [[nodiscard]] std::string takeError() noexcept;
        [[nodiscard]] sequence::SequenceNodeId movieNode() const noexcept { return movie_; }
        [[nodiscard]] const std::string& movieFile() const noexcept { return movieFile_; }
    private:
        void stopOwnedNodes(engine::SequencePlayback& playback);
        void finish(engine::SequencePlayback& playback);
        void startNextMovie(engine::SequencePlayback& playback);

        Phase phase_{Phase::Idle};
        std::uint64_t trademarkStarted_{};
        sequence::SequenceNodeId trademark_{};
        sequence::SequenceNodeId movie_{};
        std::size_t nextMovie_{};
        bool use3DBoard_{};
        bool selectionRequested_{};
        std::string movieFile_;
        std::string error_;
    };
}
