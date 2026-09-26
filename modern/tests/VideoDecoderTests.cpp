#include "VideoDecoder.hpp"
#include "VideoDecoderFixture.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
    using namespace monopoly;
    using Clock = std::chrono::steady_clock;

    void require(bool condition, const std::string& message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    video::DecoderOptions options()
    {
        video::DecoderOptions result;
        result.ffmpeg = test::ffmpegExecutable();
        if (const auto* path = SDL_getenv("MONOPOLY_TEST_FFPROBE")) result.ffprobe = path;
        return result;
    }

    template<class Predicate>
    void until(video::Decoder& decoder, Predicate predicate)
    {
        const auto deadline = Clock::now() + std::chrono::seconds(25);
        while (Clock::now() < deadline)
        {
            const auto state = decoder.snapshot();
            if (state.phase == video::DecoderPhase::Failed) throw std::runtime_error(state.error);
            if (predicate(state)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        throw std::runtime_error("Timed out waiting for real FFmpeg decoding");
    }

    void expectFailure(video::Decoder& decoder, const std::string& expected)
    {
        const auto deadline = Clock::now() + std::chrono::seconds(20);
        while (Clock::now() < deadline)
        {
            const auto state = decoder.snapshot();
            if (state.phase == video::DecoderPhase::Failed)
            {
                require(state.error.find(expected) != std::string::npos,
                    "Expected decoder diagnostic '" + expected + "', received: " + state.error);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        throw std::runtime_error("Decoder failure did not reach the UI snapshot");
    }

    void compressedVideoAndAudio(const std::filesystem::path& file)
    {
        video::Decoder decoder;
        require(decoder.open(file, options()).has_value(), "Open real compressed AVI");
        std::size_t frames{}, pcmBytes{};
        bool heardAudio = false;
        until(decoder, [&](const video::DecoderSnapshot& state)
        {
            while (auto frame = decoder.popVideo())
            {
                require(frame->width == 32 && frame->height == 24 && frame->rgba.size() == 32 * 24 * 4,
                    "Decoded RGBA geometry matches the compressed source");
                require(frame->timestampMicroseconds == frames * 100000,
                    "Each decoded frame carries its rational-cadence timestamp");
                const auto pixel = (12 * 32 + 16) * 4;
                require(frame->rgba[pixel + 3] == 255, "Decoded alpha is opaque");
                if (frames < 10)
                    require(frame->rgba[pixel] > 200 && frame->rgba[pixel + 2] < 40, "First second contains actual red pixels");
                else
                    require(frame->rgba[pixel] < 40 && frame->rgba[pixel + 2] > 200, "Second second contains actual blue pixels");
                ++frames;
            }
            while (auto audio = decoder.popAudio())
            {
                require(audio->pcm.size() % 4 == 0 && audio->pcm.size() <= 3840,
                    "Audio chunks are bounded stereo S16LE frames");
                require(audio->timestampMicroseconds == (pcmBytes / 4) * 1000000ULL / 48000,
                    "PCM timestamps derive from exact decoded sample counts");
                for (std::size_t index = 0; index + 1 < audio->pcm.size(); index += 2)
                    heardAudio |= audio->pcm[index] != 0 || audio->pcm[index + 1] != 0;
                pcmBytes += audio->pcm.size();
            }
            return state.phase == video::DecoderPhase::Ended &&
                decoder.snapshot().queuedVideoFrames == 0 && decoder.snapshot().queuedAudioChunks == 0;
        });
        const auto finished = decoder.snapshot();
        require(finished.metadata && finished.metadata->videoCodec == "mpeg4" &&
            finished.metadata->hasAudio && finished.metadata->audioSampleRate == 48'000U &&
            finished.metadata->frameRateNumerator == 10 &&
            finished.metadata->frameRateDenominator == 1 && finished.metadata->durationMicroseconds == 2000000,
            "Real FFprobe metadata describes MPEG-4 AVI and native audio rate");
        require(frames == 20 && pcmBytes == 2 * 48000 * 4 && heardAudio,
            "Decoded all twenty compressed frames and two seconds of audible PCM");
        require(finished.videoEnded && finished.audioEnded, "Both stream EOF states are explicit");
        require(!decoder.seek(2000000), "Reject seek beyond final frame");
        require(decoder.seek(0).has_value(), "Seek restarts a fully consumed movie");
        until(decoder, [&](const video::DecoderSnapshot&) { return decoder.snapshot().queuedVideoFrames != 0; });
        require(decoder.popVideo()->timestampMicroseconds == 0, "Restart returns the first frame timestamp");
        std::cout << "[PASS] compressed MPEG-4 AVI pixels, real PCM, timestamps, EOF and restart\n";
    }

    void nativeAudioRateSurvivesDecodeNormalization()
    {
        test::VideoDecoderFixture nativeRateMovie(true, 22'050U);
        video::Decoder decoder;
        require(decoder.open(nativeRateMovie.file(), options()).has_value(),
            "Open movie with non-48k native audio");
        until(decoder, [](const video::DecoderSnapshot& state)
        {
            return state.metadata.has_value() && state.queuedAudioChunks != 0;
        });
        const auto snapshot = decoder.snapshot();
        const auto pcm = decoder.popAudio();
        require(snapshot.metadata && snapshot.metadata->hasAudio &&
            snapshot.metadata->audioSampleRate == 22'050U,
            "FFprobe preserves the movie's native audio sample rate");
        require(pcm && video::DecodedAudioChunk::sampleRate == 48'000U,
            "decoder PCM remains normalized to 48k independently of native pitch metadata");
        decoder.stop();
    }

    void backpressureSeekAndStop(const std::filesystem::path& file)
    {
        video::Decoder decoder;
        auto config = options();
        config.videoQueueFrames = 1;
        config.audioQueueChunks = 1;
        require(decoder.open(file, config).has_value(), "Open bounded movie decoder");
        until(decoder, [](const video::DecoderSnapshot& state)
        { return state.queuedVideoFrames == 1 && state.queuedAudioChunks == 1; });
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        require(decoder.snapshot().queuedVideoFrames == 1 && decoder.snapshot().queuedAudioChunks == 1,
            "Full consumer queues impose backpressure on both subprocesses");
        const auto generation = decoder.snapshot().generation;
        const auto seekStart = Clock::now();
        require(decoder.seek(1000000).has_value(), "Seek while both output queues are full");
        require(Clock::now() - seekStart < std::chrono::milliseconds(100), "Seek posts an asynchronous command");
        require(decoder.snapshot().generation == generation + 1, "Seek invalidates old output generation");
        until(decoder, [](const video::DecoderSnapshot& state)
        { return state.queuedVideoFrames == 1 && state.queuedAudioChunks == 1; });
        const auto frame = decoder.popVideo();
        const auto audio = decoder.popAudio();
        require(frame && frame->timestampMicroseconds == 1000000 && frame->rgba[2] > 200,
            "Seek decodes the requested blue frame with no stale red output");
        require(audio && audio->timestampMicroseconds == 1000000,
            "Seek aligns PCM to the same movie timeline");
        until(decoder, [](const video::DecoderSnapshot& state)
        { return state.queuedVideoFrames == 1 && state.queuedAudioChunks == 1; });
        const auto stopStart = Clock::now();
        decoder.stop();
        require(Clock::now() - stopStart < std::chrono::seconds(2), "Stop cancels backpressure without draining movie output");
        require(decoder.snapshot().phase == video::DecoderPhase::Stopped &&
            !decoder.popVideo() && !decoder.popAudio(), "Stop clears processes, metadata and output queues");
        decoder.stop();
        std::cout << "[PASS] bounded queues, seek during backpressure, cancellation and repeated stop\n";
    }

    void errorsAndStreamSelection(const std::filesystem::path& file)
    {
        video::Decoder decoder;
        auto config = options();
        config.ffprobe = "monopoly-deliberately-missing-ffprobe-executable";
        require(decoder.open(file, config).has_value(), "Missing dependency is reported asynchronously");
        expectFailure(decoder, "install FFmpeg/FFprobe");
        config = options();
        config.ffmpeg = "monopoly-deliberately-missing-ffmpeg-executable";
        require(decoder.open(file, config).has_value(), "Open with missing decode executable");
        expectFailure(decoder, "install FFmpeg/FFprobe");
        config = options();
        config.maximumFrameBytes = 64;
        require(decoder.open(file, config).has_value(), "Probe before allocating bounded video frames");
        expectFailure(decoder, "memory limit");
        config = options();
        config.videoQueueFrames = 0;
        require(!decoder.open(file, config), "Reject zero-capacity consumer queues");
        require(!decoder.open(file.parent_path() / "missing.avi", options()), "Missing movie file is explicit");
        const auto corrupt = file.parent_path() / "corrupt.avi";
        { std::ofstream output(corrupt, std::ios::binary); output << "not a movie"; }
        require(decoder.open(corrupt, options()).has_value(), "Open corrupt container for diagnosis");
        expectFailure(decoder, "FFprobe rejected input");

        config = options();
        config.decodeVideo = false;
        require(decoder.open(file, config).has_value(), "Open audio-only decode of the movie");
        std::size_t bytes{};
        until(decoder, [&](const video::DecoderSnapshot& state)
        {
            while (auto chunk = decoder.popAudio()) bytes += chunk->pcm.size();
            require(!decoder.popVideo(), "Disabled video produces no RGBA queue");
            return state.phase == video::DecoderPhase::Ended && decoder.snapshot().queuedAudioChunks == 0;
        });
        require(bytes == 2 * 48000 * 4 && decoder.snapshot().videoEnded,
            "Audio-only selection still decodes all PCM");

        test::VideoDecoderFixture silent(false);
        require(decoder.open(silent.file(), options()).has_value(), "Open real silent compressed movie");
        std::size_t frames{};
        until(decoder, [&](const video::DecoderSnapshot& state)
        {
            while (decoder.popVideo()) ++frames;
            require(!decoder.popAudio(), "Silent movie never fabricates audio");
            return state.phase == video::DecoderPhase::Ended && decoder.snapshot().queuedVideoFrames == 0;
        });
        require(frames == 20 && decoder.snapshot().metadata && !decoder.snapshot().metadata->hasAudio &&
            decoder.snapshot().audioEnded, "Silent movie completes independently of an audio device");
        std::cout << "[PASS] dependency/corruption/capacity diagnostics, stream selection and real silent video\n";
    }
}

int main()
{
    try
    {
        monopoly::test::VideoDecoderFixture fixture;
        compressedVideoAndAudio(fixture.file());
        nativeAudioRateSurvivesDecodeNormalization();
        std::cout << "[PASS] native movie audio rate survives 48k decode normalization\n";
        backpressureSeekAndStop(fixture.file());
        errorsAndStreamSelection(fixture.file());
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
