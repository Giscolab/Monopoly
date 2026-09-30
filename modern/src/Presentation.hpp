#pragma once

#include <SDL3/SDL.h>

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
}
