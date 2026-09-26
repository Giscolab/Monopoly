#include "VideoDecoder.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace monopoly::video
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        constexpr std::size_t diagnosticLimit = 16384;
        constexpr std::size_t probeLimit = 65536;
        constexpr std::size_t audioChunkBytes = 960 * 4;
        constexpr std::uint64_t maximumDuration = 24ULL * 60 * 60 * 1000000;

        std::string utf8Path(const std::filesystem::path& path)
        {
            const auto bytes = path.u8string();
            return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        }

        std::string seconds(std::uint64_t microseconds)
        {
            auto fraction = std::to_string(microseconds % 1000000);
            return std::to_string(microseconds / 1000000) + "." +
                std::string(6 - fraction.size(), '0') + fraction;
        }

        template<class T>
        bool number(std::string_view text, T& value)
        {
            const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            return result.ec == std::errc{} && result.ptr == text.data() + text.size();
        }

        bool rational(std::string_view text, std::uint32_t& numerator,
            std::uint32_t& denominator)
        {
            const auto slash = text.find('/');
            if (slash == std::string_view::npos ||
                !number(text.substr(0, slash), numerator) ||
                !number(text.substr(slash + 1), denominator)) return false;
            return numerator > 0 && numerator <= 1000000 && denominator > 0 &&
                denominator <= 1000000 &&
                static_cast<double>(numerator) / denominator <= 240.0;
        }

        std::expected<DecoderMetadata, std::string> parseProbe(std::string_view text)
        {
            DecoderMetadata result;
            struct Stream
            {
                std::string kind, codec, average, rate;
                std::uint32_t width{}, height{}, sampleRate{};
                double duration{};
            } stream;
            bool inStream = false;
            double formatDuration{};
            std::istringstream input{std::string(text)};
            std::string line;
            while (std::getline(input, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line == "[STREAM]") { stream = {}; inStream = true; continue; }
                if (line == "[/STREAM]")
                {
                    if (stream.kind == "video" && result.videoCodec.empty())
                    {
                        result.width = stream.width;
                        result.height = stream.height;
                        result.videoCodec = stream.codec;
                        if (!rational(stream.average, result.frameRateNumerator,
                                result.frameRateDenominator) &&
                            !rational(stream.rate, result.frameRateNumerator,
                                result.frameRateDenominator))
                            return std::unexpected("FFprobe: video has no supported rational cadence (maximum 240 fps)");
                        if (std::isfinite(stream.duration) && stream.duration > 0 &&
                            stream.duration <= static_cast<double>(maximumDuration / 1000000))
                            result.durationMicroseconds = static_cast<std::uint64_t>(stream.duration * 1000000.0);
                    }
                    else if (stream.kind == "audio" && !result.hasAudio)
                    {
                        result.hasAudio = true;
                        result.audioSampleRate = stream.sampleRate;
                        result.audioCodec = stream.codec;
                    }
                    inStream = false;
                    continue;
                }
                const auto split = line.find('=');
                if (split == std::string::npos) continue;
                const std::string_view key(line.data(), split);
                const std::string_view value(line.data() + split + 1, line.size() - split - 1);
                if (!inStream)
                {
                    if (key == "duration") number(value, formatDuration);
                }
                else if (key == "codec_type") stream.kind = value;
                else if (key == "codec_name") stream.codec = value;
                else if (key == "width") number(value, stream.width);
                else if (key == "height") number(value, stream.height);
                else if (key == "avg_frame_rate") stream.average = value;
                else if (key == "r_frame_rate") stream.rate = value;
                else if (key == "sample_rate") number(value, stream.sampleRate);
                else if (key == "duration") number(value, stream.duration);
            }
            if (result.videoCodec.empty() || result.width == 0 || result.height == 0)
                return std::unexpected("FFprobe: input has no video stream with valid dimensions");
            if (result.width > 8192 || result.height > 8192)
                return std::unexpected("FFprobe: video dimension exceeds the 8192-pixel limit");
            if (result.durationMicroseconds == 0 && std::isfinite(formatDuration) &&
                formatDuration > 0 && formatDuration <= static_cast<double>(maximumDuration / 1000000))
                result.durationMicroseconds = static_cast<std::uint64_t>(formatDuration * 1000000.0);
            if (result.durationMicroseconds == 0)
                return std::unexpected("FFprobe: input has no finite movie duration within the 24-hour limit");
            return result;
        }

        // All methods, including destruction, run on the decoder's single worker.
        // SDL process pipes are nonblocking, so cancellation never waits for IO.
        struct Child
        {
            SDL_Process* process{};
            SDL_IOStream* output{};
            SDL_IOStream* errors{};
            bool outputEnded{};
            bool errorEnded{};
            bool exited{};
            int exitCode{};
            std::string diagnostics;
            std::string ioError;
#if defined(_WIN32)
            HANDLE exitStatusHandle{};
#endif

            ~Child() { close(); }
            Child() = default;
            Child(const Child&) = delete;
            Child& operator=(const Child&) = delete;

            void close() noexcept
            {
                if (!process) return;
                if (!exited)
                {
                    SDL_KillProcess(process, true);
                    SDL_WaitProcess(process, true, &exitCode);
                }
#if defined(_WIN32)
                if (exitStatusHandle) CloseHandle(exitStatusHandle);
                exitStatusHandle = nullptr;
#endif
                SDL_DestroyProcess(process);
                process = nullptr;
                output = errors = nullptr;
            }

            std::expected<void, std::string> start(const std::vector<std::string>& arguments)
            {
                std::vector<const char*> argv;
                argv.reserve(arguments.size() + 1);
                for (const auto& argument : arguments) argv.push_back(argument.c_str());
                argv.push_back(nullptr);
                const auto properties = SDL_CreateProperties();
                if (!properties) return std::unexpected(std::string("SDL process properties: ") + SDL_GetError());
                bool configured = SDL_SetPointerProperty(properties,
                    SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data()) &&
                    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL) &&
                    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP) &&
                    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_APP);
#if defined(_WIN32)
                // SDL's background flag supplies CREATE_NO_WINDOW, including when
                // Monopoly has no parent console. SDL then deliberately masks the
                // exit code; a query-only handle retains the real Windows status.
                configured = configured && SDL_SetBooleanProperty(properties,
                    SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
#endif
                if (configured) process = SDL_CreateProcessWithProperties(properties);
                const std::string failure = process ? std::string{} : SDL_GetError();
                SDL_DestroyProperties(properties);
                if (!process) return std::unexpected("Cannot start " + arguments.front() +
                    "; install FFmpeg/FFprobe or configure their executable paths: " + failure);
#if defined(_WIN32)
                const auto processId = SDL_GetNumberProperty(SDL_GetProcessProperties(process),
                    SDL_PROP_PROCESS_PID_NUMBER, 0);
                exitStatusHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                    static_cast<DWORD>(processId));
                if (!exitStatusHandle)
                    return std::unexpected("Cannot retain decoder process exit status (Windows error " +
                        std::to_string(GetLastError()) + ")");
#endif
                output = SDL_GetProcessOutput(process);
                errors = static_cast<SDL_IOStream*>(SDL_GetPointerProperty(
                    SDL_GetProcessProperties(process), SDL_PROP_PROCESS_STDERR_POINTER, nullptr));
                if (!output || !errors) return std::unexpected("SDL did not create the requested decoder pipes");
                return {};
            }

            std::size_t read(SDL_IOStream* pipe, void* destination, std::size_t capacity,
                bool& ended)
            {
                if (ended) return 0;
                const auto count = SDL_ReadIO(pipe, destination, capacity);
                const auto status = SDL_GetIOStatus(pipe);
                if (status == SDL_IO_STATUS_EOF) ended = true;
                else if (status == SDL_IO_STATUS_ERROR)
                {
                    ended = true;
                    ioError = SDL_GetError();
                }
                return count;
            }

            void poll()
            {
                std::array<char, 4096> buffer{};
                // Continue draining after the diagnostic cap, avoiding a stderr
                // pipe deadlock without retaining unbounded child output.
                for (int readCount = 0; readCount < 8 && !errorEnded; ++readCount)
                {
                    const auto count = read(errors, buffer.data(), buffer.size(), errorEnded);
                    diagnostics.append(buffer.data(), std::min(count, diagnosticLimit - diagnostics.size()));
                    if (count == 0) break;
                }
                if (!exited)
                {
                    exited = SDL_WaitProcess(process, false, &exitCode);
#if defined(_WIN32)
                    if (exited)
                    {
                        DWORD actualExitCode{};
                        if (GetExitCodeProcess(exitStatusHandle, &actualExitCode))
                            exitCode = static_cast<int>(actualExitCode);
                        else ioError = "Cannot query decoder process exit status (Windows error " +
                            std::to_string(GetLastError()) + ")";
                    }
#endif
                }
            }

            std::string failure(std::string_view operation) const
            {
                return std::string(operation) + " (exit " + std::to_string(exitCode) + "): " +
                    (ioError.empty() ? diagnostics : ioError);
            }
        };
    }

    struct Decoder::Impl
    {
        mutable std::mutex mutex;
        std::condition_variable wake;
        std::atomic<bool> stopping{false};
        std::thread worker;
        DecoderSnapshot state;
        DecoderOptions options;
        std::filesystem::path file;
        std::deque<DecodedVideoFrame> video;
        std::deque<DecodedAudioChunk> audio;
        std::optional<std::uint64_t> pendingSeek;

        void pause()
        {
            std::unique_lock lock(mutex);
            wake.wait_for(lock, std::chrono::milliseconds(2));
        }

        void fail(std::string error)
        {
            std::lock_guard lock(mutex);
            if (stopping.load()) return;
            state.phase = DecoderPhase::Failed;
            state.error = std::move(error);
        }

        std::expected<DecoderMetadata, std::string> probe()
        {
            Child child;
            const auto started = child.start({options.ffprobe, "-v", "error",
                "-protocol_whitelist", "file,pipe", "-show_entries",
                "stream=codec_type,codec_name,width,height,avg_frame_rate,r_frame_rate,sample_rate,duration:format=duration",
                "-of", "default=noprint_wrappers=0", utf8Path(file)});
            if (!started) return std::unexpected(started.error());
            const auto deadline = Clock::now() + std::chrono::milliseconds(options.probeTimeoutMilliseconds);
            std::string text;
            std::array<char, 4096> buffer{};
            while (!stopping.load())
            {
                child.poll();
                const auto count = child.read(child.output, buffer.data(), buffer.size(), child.outputEnded);
                if (text.size() + count > probeLimit)
                    return std::unexpected("FFprobe metadata exceeds the 64 KiB limit");
                text.append(buffer.data(), count);
                if (!child.ioError.empty()) return std::unexpected(child.failure("FFprobe IO failure"));
                if (child.outputEnded && child.errorEnded && child.exited)
                {
                    if (child.exitCode != 0) return std::unexpected(child.failure("FFprobe rejected input"));
                    return parseProbe(text);
                }
                if (Clock::now() >= deadline) return std::unexpected("FFprobe metadata timeout");
                if (count == 0) pause();
            }
            return std::unexpected("Video probe cancelled");
        }

        std::vector<std::string> command(std::uint64_t offset, bool isVideo,
            const DecoderMetadata& metadata) const
        {
            std::vector<std::string> args{options.ffmpeg, "-nostdin", "-hide_banner", "-v", "error", "-xerror",
                "-max_alloc", "67108864", "-threads", "1", "-protocol_whitelist", "file,pipe",
                "-ss", seconds(offset), "-noautorotate", "-i", utf8Path(file),
                "-t", seconds(metadata.durationMicroseconds - offset), "-threads", "1"};
            if (isVideo)
            {
                const std::string filter = "fps=fps=" + std::to_string(metadata.frameRateNumerator) +
                    "/" + std::to_string(metadata.frameRateDenominator) + ":start_time=0,scale=" +
                    std::to_string(metadata.width) + ":" + std::to_string(metadata.height);
                args.insert(args.end(), {"-map", "0:v:0", "-an", "-sn", "-dn", "-vf", filter,
                    "-fps_mode", "passthrough", "-pix_fmt", "rgba", "-c:v", "rawvideo",
                    "-f", "rawvideo", "pipe:1"});
            }
            else args.insert(args.end(), {"-map", "0:a:0", "-vn", "-sn", "-dn",
                "-af", "aresample=48000:async=1:first_pts=0", "-ac", "2", "-ar", "48000",
                "-c:a", "pcm_s16le", "-f", "s16le", "pipe:1"});
            return args;
        }

        // Returns to the outer loop on seek, EOF, failure or cancellation.
        void decode(const DecoderMetadata& metadata, std::uint64_t offset,
            std::uint64_t generation)
        {
            Child videoChild, audioChild;
            std::expected<void, std::string> started;
            if (options.decodeVideo)
            {
                started = videoChild.start(command(offset, true, metadata));
                if (!started) { fail(started.error()); return; }
            }
            if (metadata.hasAudio && options.decodeAudio)
            {
                started = audioChild.start(command(offset, false, metadata));
                if (!started) { fail(started.error()); return; }
            }
            const auto frameBytes = static_cast<std::size_t>(metadata.width) * metadata.height * 4;
            std::vector<std::uint8_t> frame(frameBytes), samples(audioChunkBytes);
            std::size_t frameUsed{}, samplesUsed{};
            std::uint64_t frameIndex{}, sampleIndex{};
            bool videoDone = !options.decodeVideo;
            bool audioDone = !metadata.hasAudio || !options.decodeAudio;
            {
                std::lock_guard lock(mutex);
                if (generation != state.generation) return;
                state.videoEnded = videoDone;
                state.audioEnded = audioDone;
            }
            auto lastProgress = Clock::now();
            while (!stopping.load())
            {
                bool videoRoom{}, audioRoom{};
                {
                    std::lock_guard lock(mutex);
                    if (generation != state.generation) return;
                    videoRoom = video.size() < options.videoQueueFrames;
                    audioRoom = audio.size() < options.audioQueueChunks;
                }
                if (options.decodeVideo) videoChild.poll();
                if (metadata.hasAudio && options.decodeAudio) audioChild.poll();
                bool progressed = false;
                if (!videoDone && videoRoom)
                {
                    const auto count = videoChild.read(videoChild.output, frame.data() + frameUsed,
                        frame.size() - frameUsed, videoChild.outputEnded);
                    frameUsed += count;
                    progressed |= count != 0;
                    if (frameUsed == frame.size())
                    {
                        DecodedVideoFrame ready{offset + frameIndex * 1000000ULL *
                            metadata.frameRateDenominator / metadata.frameRateNumerator,
                            metadata.width, metadata.height, std::move(frame)};
                        {
                            std::lock_guard lock(mutex);
                            if (generation != state.generation) return;
                            video.push_back(std::move(ready));
                        }
                        frame.resize(frameBytes);
                        frameUsed = 0;
                        ++frameIndex;
                    }
                    if (videoChild.outputEnded && videoChild.errorEnded && videoChild.exited)
                    {
                        if (videoChild.exitCode != 0 || !videoChild.ioError.empty())
                        { fail(videoChild.failure("FFmpeg video decoder " + metadata.videoCodec)); return; }
                        if (frameUsed != 0 || frameIndex == 0)
                        { fail("FFmpeg video decoder " + metadata.videoCodec + " returned an incomplete or empty RGBA stream"); return; }
                        const auto expectedFrames = (metadata.durationMicroseconds - offset) *
                            metadata.frameRateNumerator / (1000000ULL * metadata.frameRateDenominator);
                        // Allow one frame for endpoint rounding at a fractional
                        // seek position; a silently truncated movie is not EOF.
                        if (frameIndex + 1 < expectedFrames)
                        { fail("FFmpeg video decoder " + metadata.videoCodec + " ended before the declared movie duration"); return; }
                        videoDone = true;
                        std::lock_guard lock(mutex);
                        if (generation == state.generation) state.videoEnded = true;
                    }
                }
                if (!audioDone && audioRoom)
                {
                    const auto count = audioChild.read(audioChild.output, samples.data() + samplesUsed,
                        samples.size() - samplesUsed, audioChild.outputEnded);
                    samplesUsed += count;
                    progressed |= count != 0;
                    if (samplesUsed == samples.size() || (audioChild.outputEnded && samplesUsed != 0))
                    {
                        if (samplesUsed % 4 != 0)
                        { fail("FFmpeg audio decoder " + metadata.audioCodec + " returned incomplete stereo S16LE PCM"); return; }
                        samples.resize(samplesUsed);
                        DecodedAudioChunk ready{offset + sampleIndex * 1000000ULL /
                            DecodedAudioChunk::sampleRate, std::move(samples)};
                        sampleIndex += samplesUsed / 4;
                        {
                            std::lock_guard lock(mutex);
                            if (generation != state.generation) return;
                            audio.push_back(std::move(ready));
                        }
                        samples.resize(audioChunkBytes);
                        samplesUsed = 0;
                    }
                    if (audioChild.outputEnded && audioChild.errorEnded && audioChild.exited)
                    {
                        if (audioChild.exitCode != 0 || !audioChild.ioError.empty())
                        { fail(audioChild.failure("FFmpeg audio decoder " + metadata.audioCodec)); return; }
                        audioDone = true;
                        std::lock_guard lock(mutex);
                        if (generation == state.generation) state.audioEnded = true;
                    }
                }
                if (videoDone && audioDone)
                {
                    std::lock_guard lock(mutex);
                    if (generation == state.generation) state.phase = DecoderPhase::Ended;
                    return;
                }
                if (progressed || (!videoDone && !videoRoom) || (!audioDone && !audioRoom))
                    lastProgress = Clock::now();
                else if (Clock::now() - lastProgress > std::chrono::seconds(30))
                { fail("FFmpeg decode timed out without output for video codec " + metadata.videoCodec); return; }
                if (!progressed) pause();
            }
        }

        void run() noexcept
        {
            try
            {
                const auto metadata = probe();
                if (!metadata) { fail(metadata.error()); return; }
                const auto frameBytes = static_cast<std::uint64_t>(metadata->width) * metadata->height * 4;
                if (frameBytes > options.maximumFrameBytes)
                { fail("Decoded RGBA frame exceeds the configured video memory limit"); return; }
                {
                    std::lock_guard lock(mutex);
                    state.metadata = *metadata;
                    state.phase = DecoderPhase::Decoding;
                }
                std::uint64_t offset{}, generation{};
                while (!stopping.load())
                {
                    {
                        std::lock_guard lock(mutex);
                        generation = state.generation;
                        if (pendingSeek) { offset = *pendingSeek; pendingSeek.reset(); }
                        state.phase = DecoderPhase::Decoding;
                        state.error.clear();
                    }
                    decode(*metadata, offset, generation);
                    if (stopping.load()) break;
                    std::unique_lock lock(mutex);
                    if (state.phase == DecoderPhase::Failed) break;
                    wake.wait(lock, [&] { return stopping.load() || pendingSeek.has_value(); });
                }
            }
            catch (const std::exception& error) { fail(std::string("Video decoder worker: ") + error.what()); }
            catch (...) { fail("Video decoder worker: unknown failure"); }
        }
    };

    Decoder::Decoder() : impl_(std::make_unique<Impl>()) {}
    Decoder::~Decoder() { stop(); }

    std::expected<void, std::string> Decoder::open(const std::filesystem::path& file,
        const DecoderOptions& options)
    {
        if (options.ffmpeg.empty() || options.ffprobe.empty())
            return std::unexpected("FFmpeg and FFprobe executable paths must not be empty");
        if ((!options.decodeVideo && !options.decodeAudio) ||
            options.videoQueueFrames == 0 || options.videoQueueFrames > 16 ||
            options.audioQueueChunks == 0 || options.audioQueueChunks > 256 ||
            options.maximumFrameBytes < 4 || options.maximumFrameBytes > 16U * 1024U * 1024U ||
            options.videoQueueFrames * options.maximumFrameBytes > 64U * 1024U * 1024U ||
            options.probeTimeoutMilliseconds == 0 || options.probeTimeoutMilliseconds > 60000)
            return std::unexpected("Invalid decoder limits (RGBA queues maximum 64 MiB, frame maximum 16 MiB)");
        std::error_code error;
        const auto absolute = std::filesystem::absolute(file, error);
        if (error || !std::filesystem::is_regular_file(absolute, error))
            return std::unexpected("Video input is not a readable local file: " + utf8Path(file));
        stop();
        impl_->options = options;
        impl_->file = absolute;
        impl_->stopping.store(false);
        {
            std::lock_guard lock(impl_->mutex);
            impl_->state.phase = DecoderPhase::Probing;
            ++impl_->state.generation;
        }
        try { impl_->worker = std::thread([this] { impl_->run(); }); }
        catch (const std::exception& exception)
        {
            const auto failure = std::string("Cannot start video decoder worker: ") + exception.what();
            impl_->fail(failure);
            return std::unexpected(failure);
        }
        return {};
    }

    std::expected<void, std::string> Decoder::seek(std::uint64_t timestampMicroseconds)
    {
        {
            std::lock_guard lock(impl_->mutex);
            if (!impl_->state.metadata || impl_->state.phase == DecoderPhase::Failed ||
                impl_->state.phase == DecoderPhase::Stopped)
                return std::unexpected("Cannot seek before movie metadata is ready or after decoder failure");
            if (timestampMicroseconds >= impl_->state.metadata->durationMicroseconds)
                return std::unexpected("Video seek position is outside the movie duration");
            impl_->video.clear();
            impl_->audio.clear();
            impl_->pendingSeek = timestampMicroseconds;
            ++impl_->state.generation;
            impl_->state.phase = DecoderPhase::Decoding;
            impl_->state.videoEnded = false;
            impl_->state.audioEnded = false;
        }
        impl_->wake.notify_all();
        return {};
    }

    void Decoder::stop() noexcept
    {
        impl_->stopping.store(true);
        impl_->wake.notify_all();
        if (impl_->worker.joinable()) impl_->worker.join();
        std::lock_guard lock(impl_->mutex);
        impl_->video.clear();
        impl_->audio.clear();
        impl_->pendingSeek.reset();
        const auto generation = impl_->state.generation;
        impl_->state = {};
        impl_->state.generation = generation;
    }

    DecoderSnapshot Decoder::snapshot() const
    {
        std::lock_guard lock(impl_->mutex);
        auto state = impl_->state;
        state.queuedVideoFrames = impl_->video.size();
        state.queuedAudioChunks = impl_->audio.size();
        return state;
    }

    std::optional<DecodedVideoFrame> Decoder::popVideo()
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->video.empty()) return std::nullopt;
        auto frame = std::move(impl_->video.front());
        impl_->video.pop_front();
        impl_->wake.notify_all();
        return frame;
    }

    std::optional<DecodedAudioChunk> Decoder::popAudio()
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->audio.empty()) return std::nullopt;
        auto chunk = std::move(impl_->audio.front());
        impl_->audio.pop_front();
        impl_->wake.notify_all();
        return chunk;
    }
}
