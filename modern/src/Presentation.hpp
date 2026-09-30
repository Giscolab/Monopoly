#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace monopoly::presentation
{
    enum class WindowMode
    {
        Windowed,
        BorderlessFullscreen,
        ExclusiveFullscreen
    };

    enum class PresentMode
    {
        Vsync,
        Mailbox,
        Immediate
    };

    struct Options
    {
        WindowMode windowMode{WindowMode::BorderlessFullscreen};
        PresentMode presentMode{PresentMode::Vsync};
        int width{1600};
        int height{900};
        bool resolutionExplicit{};
        std::vector<std::string_view> remaining;
    };

    [[nodiscard]] std::expected<Options, std::string>
    parseArguments(std::span<const std::string_view> arguments);

    [[nodiscard]] SDL_WindowFlags windowFlags(
        const Options& options) noexcept;

    [[nodiscard]] std::pair<int, int> initialWindowSize(
        const Options& options) noexcept;

    [[nodiscard]] std::expected<void, std::string>
    configureWindow(SDL_Window* window, const Options& options);

    // F11 convenience path: desktop-resolution borderless fullscreen <-> the
    // configured development window size. It does not alter gameplay state.
    [[nodiscard]] std::expected<void, std::string>
    toggleBorderlessFullscreen(
        SDL_Window* window,
        const Options& options);

    [[nodiscard]] SDL_GPUPresentMode toSDLPresentMode(
        PresentMode mode) noexcept;

    [[nodiscard]] std::string_view presentModeName(
        PresentMode mode) noexcept;

    [[nodiscard]] std::string_view windowModeName(
        WindowMode mode) noexcept;

    // Applies the requested presentation mode after the SDL_GPU swapchain is
    // created. Unsupported mailbox/immediate requests fall back to VSync.
    [[nodiscard]] std::expected<SDL_GPUPresentMode, std::string>
    configureSwapchain(
        SDL_GPUDevice* device,
        SDL_Window* window,
        PresentMode requested);

    // Lightweight runtime evidence for the modernization target. The title is
    // refreshed roughly once per second with physical swapchain-scale pixels
    // and measured submitted frames per second.
    class FrameTelemetry final
    {
    public:
        explicit FrameTelemetry(SDL_Window* window) noexcept;
        void framePresented() noexcept;
        [[nodiscard]] double lastFramesPerSecond() const noexcept;

    private:
        SDL_Window* window_{};
        Uint64 sampleStartNanoseconds_{};
        std::uint32_t sampleFrames_{};
        double lastFramesPerSecond_{};
    };
}
