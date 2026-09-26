#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace monopoly::test
{
    inline std::string movieUtf8Path(const std::filesystem::path& path)
    {
        const auto value = path.u8string();
        return {reinterpret_cast<const char*>(value.data()), value.size()};
    }

    inline std::string ffmpegExecutable()
    {
        const auto* overridePath = SDL_getenv("MONOPOLY_TEST_FFMPEG");
        return overridePath && *overridePath ? overridePath : "ffmpeg";
    }

    // Real compressed MPEG-4 AVI fixture, not an AVI-header-only stand-in.
    // Two seconds, 32x24, 10 fps; red for one second, then blue. Optional real
    // 440 Hz PCM audio. No claim of testing proprietary retail Indeo/Bink assets.
    class VideoDecoderFixture final
    {
    public:
        explicit VideoDecoderFixture(
            bool withAudio = true,
            std::uint32_t audioSampleRate = 48'000U)
        {
            directory_ = std::filesystem::temp_directory_path() /
                ("Monopoly video fixture " + std::to_string(SDL_GetTicksNS()));
            if (!std::filesystem::create_directory(directory_))
                throw std::runtime_error("Cannot create unique movie fixture directory");
            file_ = directory_ / "compressed movie.avi";
            try
            {
                std::vector<std::string> args{ffmpegExecutable(), "-nostdin", "-hide_banner", "-v", "error",
                    "-f", "lavfi", "-i",
                    "color=c=red:s=32x24:r=10:d=2,drawbox=x=0:y=0:w=iw:h=ih:color=blue:t=fill:enable='gte(t,1)'"};
                if (withAudio) args.insert(args.end(), {"-f", "lavfi", "-i",
                    "sine=frequency=440:sample_rate=" + std::to_string(audioSampleRate) + ":duration=2"});
                args.insert(args.end(), {"-threads", "1", "-c:v", "mpeg4", "-q:v", "2", "-pix_fmt", "yuv420p"});
                if (withAudio) args.insert(args.end(), {"-c:a", "pcm_s16le", "-ac", "2"});
                else args.push_back("-an");
                args.insert(args.end(), {"-y", movieUtf8Path(file_)});
                run(args);
            }
            catch (...) { cleanup(); throw; }
        }

        ~VideoDecoderFixture() { cleanup(); }
        VideoDecoderFixture(const VideoDecoderFixture&) = delete;
        VideoDecoderFixture& operator=(const VideoDecoderFixture&) = delete;
        const std::filesystem::path& file() const noexcept { return file_; }

    private:
        std::filesystem::path directory_, file_;

        void cleanup() noexcept
        {
            std::error_code ignored;
            // This directory was uniquely created and is owned by this fixture.
            if (!directory_.empty()) std::filesystem::remove_all(directory_, ignored);
        }

        static void run(const std::vector<std::string>& arguments)
        {
            std::vector<const char*> argv;
            for (const auto& argument : arguments) argv.push_back(argument.c_str());
            argv.push_back(nullptr);
            const auto properties = SDL_CreateProperties();
            SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
            SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
            auto* child = SDL_CreateProcessWithProperties(properties);
            SDL_DestroyProperties(properties);
            if (!child) throw std::runtime_error(std::string("FFmpeg is required for real video tests: ") + SDL_GetError());
            auto* pipe = SDL_GetProcessOutput(child);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            std::string diagnostic;
            std::array<char, 4096> bytes{};
            int exitCode{};
            bool exited = false;
            while (std::chrono::steady_clock::now() < deadline)
            {
                const auto count = SDL_ReadIO(pipe, bytes.data(), bytes.size());
                diagnostic.append(bytes.data(), std::min(count, 16384 - diagnostic.size()));
                exited = SDL_WaitProcess(child, false, &exitCode);
                if (exited && SDL_GetIOStatus(pipe) == SDL_IO_STATUS_EOF) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if (!exited)
            {
                SDL_KillProcess(child, true);
                SDL_WaitProcess(child, true, &exitCode);
            }
            SDL_DestroyProcess(child);
            if (!exited || exitCode != 0)
                throw std::runtime_error("Real compressed movie fixture generation failed: " + diagnostic);
        }
    };
}
