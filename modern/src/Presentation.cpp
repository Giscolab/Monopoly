#include "Presentation.hpp"

#include <charconv>
#include <optional>
#include <utility>

namespace monopoly::presentation
{
    namespace
    {
        [[nodiscard]] std::expected<std::pair<int, int>, std::string>
        parseResolution(std::string_view value)
        {
            const auto separator = value.find_first_of("xX");
            if (separator == std::string_view::npos ||
                separator == 0 || separator + 1 >= value.size())
                return std::unexpected(
                    "--resolution expects WIDTHxHEIGHT");

            int width{};
            int height{};
            const auto widthResult = std::from_chars(
                value.data(), value.data() + separator, width);
            const auto heightResult = std::from_chars(
                value.data() + separator + 1,
                value.data() + value.size(),
                height);
            if (widthResult.ec != std::errc{} ||
                widthResult.ptr != value.data() + separator ||
                heightResult.ec != std::errc{} ||
                heightResult.ptr != value.data() + value.size() ||
                width < 640 || height < 360 ||
                width > 16384 || height > 16384)
                return std::unexpected(
                    "--resolution must be between 640x360 and 16384x16384");

            return std::pair{width, height};
        }


        [[nodiscard]] std::optional<PresentMode>
        parsePresentMode(std::string_view value) noexcept
        {
            if (value == "vsync") return PresentMode::Vsync;
            if (value == "mailbox") return PresentMode::Mailbox;
            if (value == "immediate") return PresentMode::Immediate;
            return std::nullopt;
        }
    }


    std::expected<Options, std::string>
    parseArguments(std::span<const std::string_view> arguments)
    {
        Options result;
        bool modeExplicit = false;
        bool presentExplicit = false;

        for (std::size_t index = 0; index < arguments.size(); ++index)
        {
            const auto argument = arguments[index];

            const auto setMode = [&](WindowMode mode)
                -> std::expected<void, std::string>
            {
                if (modeExplicit)
                    return std::unexpected(
                        "window mode was specified more than once");
                result.windowMode = mode;
                modeExplicit = true;
                return {};
            };

            if (argument == "--windowed")
            {
                if (auto changed = setMode(WindowMode::Windowed); !changed)
                    return std::unexpected(changed.error());
            }
            else if (argument == "--fullscreen")
            {
                if (auto changed =
                        setMode(WindowMode::BorderlessFullscreen); !changed)
                    return std::unexpected(changed.error());
            }
            else if (argument == "--exclusive-fullscreen")
            {
                if (auto changed =
                        setMode(WindowMode::ExclusiveFullscreen); !changed)
                    return std::unexpected(changed.error());
            }
            else if (argument == "--resolution")
            {
                if (result.resolutionExplicit ||
                    index + 1 == arguments.size())
                    return std::unexpected(
                        "--resolution requires one WIDTHxHEIGHT value");
                const auto parsed = parseResolution(arguments[++index]);
                if (!parsed)
                    return std::unexpected(parsed.error());
                result.width = parsed->first;
                result.height = parsed->second;
                result.resolutionExplicit = true;
            }
            else if (argument == "--present-mode")
            {
                if (presentExplicit ||
                    index + 1 == arguments.size())
                    return std::unexpected(
                        "--present-mode requires vsync, mailbox or immediate");
                const auto parsed = parsePresentMode(arguments[++index]);
                if (!parsed)
                    return std::unexpected(
                        "--present-mode requires vsync, mailbox or immediate");
                result.presentMode = *parsed;
                presentExplicit = true;
            }
            else
            {
                result.remaining.push_back(argument);
            }
        }

        return result;
    }


    SDL_WindowFlags windowFlags(const Options& options) noexcept
    {
        SDL_WindowFlags flags =
            SDL_WINDOW_RESIZABLE |
            SDL_WINDOW_HIGH_PIXEL_DENSITY;

        if (options.windowMode != WindowMode::Windowed)
            flags |= SDL_WINDOW_FULLSCREEN;

        return flags;
    }


    std::pair<int, int> initialWindowSize(
        const Options& options) noexcept
    {
        return {options.width, options.height};
    }


    std::expected<void, std::string>
    configureWindow(SDL_Window* window, const Options& options)
    {
        if (!window)
            return std::unexpected("presentation window is null");

        if (options.windowMode == WindowMode::Windowed)
        {
            if (!SDL_SetWindowFullscreen(window, false))
                return std::unexpected(SDL_GetError());
            if (!SDL_SetWindowSize(window, options.width, options.height))
                return std::unexpected(SDL_GetError());
            (void)SDL_SetWindowPosition(
                window,
                SDL_WINDOWPOS_CENTERED,
                SDL_WINDOWPOS_CENTERED);
            return {};
        }

        if (options.windowMode == WindowMode::BorderlessFullscreen)
        {
            if (!SDL_SetWindowFullscreenMode(window, nullptr) ||
                !SDL_SetWindowFullscreen(window, true))
                return std::unexpected(SDL_GetError());
            return {};
        }

        SDL_DisplayID display = SDL_GetDisplayForWindow(window);
        if (display == 0)
            display = SDL_GetPrimaryDisplay();
        if (display == 0)
            return std::unexpected("no display is available for fullscreen");

        int width = options.width;
        int height = options.height;
        float refreshRate = 0.0F;
        if (!options.resolutionExplicit)
        {
            const auto* desktop = SDL_GetDesktopDisplayMode(display);
            if (!desktop)
                return std::unexpected(SDL_GetError());
            width = desktop->w;
            height = desktop->h;
            refreshRate = desktop->refresh_rate;
        }

        SDL_DisplayMode closest{};
        if (!SDL_GetClosestFullscreenDisplayMode(
                display, width, height, refreshRate, true, &closest))
            return std::unexpected(SDL_GetError());

        if (!SDL_SetWindowFullscreenMode(window, &closest) ||
            !SDL_SetWindowFullscreen(window, true))
            return std::unexpected(SDL_GetError());

        return {};
    }


    SDL_GPUPresentMode toSDLPresentMode(PresentMode mode) noexcept
    {
        switch (mode)
        {
        case PresentMode::Mailbox:
            return SDL_GPU_PRESENTMODE_MAILBOX;
        case PresentMode::Immediate:
            return SDL_GPU_PRESENTMODE_IMMEDIATE;
        case PresentMode::Vsync:
        default:
            return SDL_GPU_PRESENTMODE_VSYNC;
        }
    }


    std::string_view presentModeName(PresentMode mode) noexcept
    {
        switch (mode)
        {
        case PresentMode::Mailbox: return "mailbox";
        case PresentMode::Immediate: return "immediate";
        case PresentMode::Vsync:
        default: return "vsync";
        }
    }


    std::string_view windowModeName(WindowMode mode) noexcept
    {
        switch (mode)
        {
        case WindowMode::Windowed: return "windowed";
        case WindowMode::ExclusiveFullscreen: return "exclusive-fullscreen";
        case WindowMode::BorderlessFullscreen:
        default: return "borderless-fullscreen";
        }
    }


    std::expected<SDL_GPUPresentMode, std::string>
    configureSwapchain(
        SDL_GPUDevice* device,
        SDL_Window* window,
        PresentMode requested)
    {
        if (!device || !window)
            return std::unexpected("GPU presentation owner is missing");

        auto selected = toSDLPresentMode(requested);
        if (!SDL_WindowSupportsGPUPresentMode(
                device, window, selected))
            selected = SDL_GPU_PRESENTMODE_VSYNC;

        if (!SDL_SetGPUSwapchainParameters(
                device,
                window,
                SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                selected))
            return std::unexpected(
                std::string("SDL_SetGPUSwapchainParameters: ") +
                SDL_GetError());

        // Two frames in flight preserves throughput while avoiding a deep
        // presentation queue. The fixed 60 Hz game clock remains independent.
        if (!SDL_SetGPUAllowedFramesInFlight(device, 2))
            return std::unexpected(
                std::string("SDL_SetGPUAllowedFramesInFlight: ") +
                SDL_GetError());

        return selected;
    }
}
