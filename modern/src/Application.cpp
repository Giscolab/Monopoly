#include "Application.hpp"
#include "AIMessageIngress.hpp"
#include "Engine.hpp"
#include "Display.hpp"
#include "SequencePlayback.hpp"
#include "Game.hpp"
#include "LogicalViewport.hpp"
#include "Messaging.hpp"
#include "MousePointer.hpp"
#include "Presentation.hpp"
#include "ModernSceneCatalog.hpp"
#include "ChatRuntime.hpp"
#include "TcpMessageTransport.hpp"
#include "StartupResources.hpp"
#include "UIMessages.hpp"
#include "UserInterface.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    struct MouseLogicalMapping
    {
        monopoly::logicalviewport::LogicalPoint point;
        double scale{};
    };


    [[nodiscard]] bool contains(
        const monopoly::logicalviewport::PixelRect& rect,
        double x,
        double y) noexcept
    {
        return x >= rect.x && y >= rect.y &&
            x < rect.x + rect.width &&
            y < rect.y + rect.height;
    }


    [[nodiscard]] std::optional<MouseLogicalMapping>
    mouseToLogical(
        SDL_Window* window,
        float x,
        float y)
    {
        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSize(window, &width, &height))
            return std::nullopt;

        const auto windowX = static_cast<double>(x);
        const auto windowY = static_cast<double>(y);
        const auto& displayState = monopoly::display::stateReadOnly();

        if (displayState.viewportInUse != monopoly::display::Viewport3D::Off &&
            monopoly::display::isBoardVisible(displayState.desired2DView))
        {
            const auto uiTransform = monopoly::logicalviewport::makeTransform(width, height);
            const auto viewport = monopoly::display::worldViewport(displayState.viewportInUse);
            const auto& trade = monopoly::userinterface::tradeStateReadOnly();
            const bool tradeModal = displayState.desired2DView == monopoly::display::Screen2D::Trade &&
                (trade.playerSelectVisible || trade.cashDialogVisible || trade.contractDialogVisible);
            if (const auto overlay = monopoly::logicalviewport::windowToUIOverlay(
                    uiTransform, windowX, windowY,
                    {static_cast<double>(viewport.left), static_cast<double>(viewport.top),
                     static_cast<double>(viewport.right - viewport.left),
                     static_cast<double>(viewport.bottom - viewport.top)}, tradeModal))
                return MouseLogicalMapping{*overlay, uiTransform.scale};
            if (tradeModal) return std::nullopt;
            const auto worldTransform =
                monopoly::logicalviewport::makeWorld3DTransform(width, height);
            const auto pixels = monopoly::logicalviewport::logicalToPixelRect(
                worldTransform,
                {static_cast<double>(viewport.left),
                 static_cast<double>(viewport.top),
                 static_cast<double>(viewport.right - viewport.left),
                 static_cast<double>(viewport.bottom - viewport.top)});

            if (contains(pixels, windowX, windowY))
                if (const auto point =
                        monopoly::logicalviewport::windowToLogical(
                            worldTransform, windowX, windowY))
                    return MouseLogicalMapping{*point, worldTransform.scale};
        }

        const auto uiTransform =
            monopoly::logicalviewport::makeTransform(width, height);
        if (const auto point = monopoly::logicalviewport::windowToLogical(
                uiTransform, windowX, windowY))
            return MouseLogicalMapping{*point, uiTransform.scale};

        return std::nullopt;
    }


    [[nodiscard]] bool initializeAIProfiles()
    {
        const char* basePath = SDL_GetBasePath();
        if (basePath == nullptr || *basePath == '\0')
        {
            std::cerr << "SDL_GetBasePath failed for AI profiles.\n";
            return false;
        }

        const auto directory = std::filesystem::path(basePath) /
            "assets" / "ai";
        const auto loaded = monopoly::ai::initializeMessageIngressProfiles(
            directory);
        if (!loaded)
        {
            std::cerr << "AI profile initialization failed: "
                      << loaded.error().detail << '\n';
            return false;
        }
        return true;
    }


    void sendMouseMessage(
        SDL_Window* window,
        monopoly::uimsg::Type type,
        float x,
        float y,
        float deltaX = 0.0F,
        float deltaY = 0.0F)
    {
        const auto point = mouseToLogical(window, x, y);

        if (type == monopoly::uimsg::Type::MouseLeftDown)
            monopoly::mouse::setLeftDown(true);
        else if (type == monopoly::uimsg::Type::MouseLeftUp)
            monopoly::mouse::setLeftDown(false);

        if (!point.has_value())
        {
            monopoly::mouse::updatePosition(-1, -1, false);
            // Un mouvement dans une bande noire doit retirer les hovers.
            // Les clics hors de la surface historique sont simplement ignores.
            if (type == monopoly::uimsg::Type::MouseMoved)
            {
                (void)monopoly::uimsg::send(
                    { type, -1, -1 }
                );
            }

            return;
        }

        std::array<std::int32_t, 2> worldPoint{
            static_cast<std::int32_t>(point->point.x),
            static_cast<std::int32_t>(point->point.y)};
        const auto* session = monopoly::engine::sequencePlayback();
        if (session)
            worldPoint = monopoly::engine::SequenceWorld2DSlot::transformPoint(
                const_cast<monopoly::engine::SequencePlayback*>(session)->world2D().screenToWorld(),
                worldPoint[0], worldPoint[1]);
        const auto previousPosition = monopoly::mouse::stateReadOnly();
        monopoly::mouse::updatePosition(worldPoint[0], worldPoint[1], true);

        std::int64_t logicalDeltaX = 0;
        std::int64_t logicalDeltaY = 0;
        if (point->scale > 0.0)
        {
            logicalDeltaX = static_cast<std::int64_t>(
                std::lround(static_cast<double>(deltaX) / point->scale));
            logicalDeltaY = static_cast<std::int64_t>(
                std::lround(static_cast<double>(deltaY) / point->scale));
        }
        if (session)
        {
            // L_Mouse computes deltas after inverse projection, including a
            // camera change since the previous mouse event.
            logicalDeltaX = static_cast<std::int64_t>(worldPoint[0]) - previousPosition.x;
            logicalDeltaY = static_cast<std::int64_t>(worldPoint[1]) - previousPosition.y;
        }
        const auto modifier = (SDL_GetModState() & SDL_KMOD_CTRL) != 0
            ? monopoly::uimsg::MouseModifierControl : 0;
        (void)monopoly::uimsg::send(
            {
                type,
                worldPoint[0],
                worldPoint[1],
                logicalDeltaX,
                logicalDeltaY,
                modifier
            }
        );
    }
}

namespace monopoly
{
    int Application::run(int argc, char** argv)
    {
        int result = 0;

        std::vector<std::string_view> arguments;
        for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
        const auto presentationOptions =
            presentation::parseArguments(arguments);
        if (!presentationOptions)
        {
            std::cerr << presentationOptions.error() << '\n';
            return 1;
        }

        const auto modernOptions = data::parseModernSceneArguments(presentationOptions->remaining);
        if (!modernOptions)
        {
            std::cerr << modernOptions.error() << '\n';
            return 1;
        }
        engine::configureModernScene(modernOptions->options);
        const auto resourceOptions = startup::parseResourceArguments(
            modernOptions->remaining);
        if (!resourceOptions)
        {
            std::cerr << resourceOptions.error() << '\n';
            return 1;
        }
        const auto contextSelected = startup::selectResourceContext(resourceOptions->context);
        if (!contextSelected)
        {
            std::cerr << contextSelected.error() << '\n';
            return 1;
        }
        const auto& networkArguments = resourceOptions->remaining;

        bool networkRequested = false;
        bool networkHost = false;
        bool gameplayRequested = false;
        std::string networkAddress;
        std::uint16_t networkPort = 0;
        if (!networkArguments.empty())
        {
            const auto mode = networkArguments[0];
            const auto usage = []
            {
                std::cerr << "Usage: MonopolyModern "
                    "[--windowed | --fullscreen | --exclusive-fullscreen] "
                    "[--resolution WIDTHxHEIGHT] "
                    "[--present-mode vsync|mailbox|immediate] "
                    "[--modern-board=retail|paris|usa|procedural] [--modern-buildings=retail|house] "
                    "[--modern-environment=retail|paris|procedural] "
                    "[--edition=usa|europe] [--language=en-us|en-uk|fr] "
                    "[--data-root <absolute folder>] "
                    "[--data-overrides <absolute manifest.tsv>] "
                    "[--check-resources] [--voice-host IPv4:port | "
                    "--voice-connect IPv4:port | --network-host IPv4:port | "
                    "--network-connect IPv4:port]\n"
                    "Network menu without arguments hosts on 0.0.0.0:28799.\n";
            };
            if (networkArguments.size() != 2 || (mode != "--voice-host" && mode != "--voice-connect" &&
                mode != "--network-host" && mode != "--network-connect"))
            {
                usage();
                return 1;
            }
            const auto endpoint = networkArguments[1];
            const auto colon = endpoint.rfind(':');
            if (colon == std::string_view::npos || colon == 0 || colon + 1 == endpoint.size())
            {
                usage();
                return 1;
            }
            unsigned port = 0;
            const auto parsed = std::from_chars(endpoint.data() + colon + 1,
                endpoint.data() + endpoint.size(), port);
            if (parsed.ec != std::errc{} || parsed.ptr != endpoint.data() + endpoint.size() ||
                port == 0 || port > 65535)
            {
                usage();
                return 1;
            }
            networkRequested = true;
            gameplayRequested = mode == "--network-host" || mode == "--network-connect";
            networkHost = mode == "--voice-host" || mode == "--network-host";
            networkAddress = endpoint.substr(0, colon);
            networkPort = static_cast<std::uint16_t>(port);
        }

        if (resourceOptions->dataRoot)
        {
            const auto selected = startup::selectResourceRoot(*resourceOptions->dataRoot);
            if (!selected)
            {
                std::cerr << "Invalid --data-root: " << selected.error() << '\n';
                return 1;
            }
        }
        if (resourceOptions->dataOverrides)
        {
            const auto selected = startup::selectDataOverrideManifest(
                *resourceOptions->dataOverrides);
            if (!selected)
            {
                std::cerr << "Invalid --data-overrides: "
                    << selected.error() << '\n';
                return 1;
            }
        }
        if (resourceOptions->checkOnly)
        {
            const auto checked = startup::prepareResources(false);
            SDL_Quit();
            return checked == startup::ResourceSetupResult::Ready ? 0 : 1;
        }

        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
            return 1;
        }

        // Audio is initialized before opening movies. A missing device is not
        // fatal to gameplay; AudioRuntime can retry later if the device changes.
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
            std::cerr << "SDL audio unavailable at startup: "
                << SDL_GetError() << '\n';

        const auto setup = startup::prepareResources(true);
        if (setup != startup::ResourceSetupResult::Ready)
        {
            SDL_Quit();
            return setup == startup::ResourceSetupResult::Cancelled ? 0 : 1;
        }

        const auto [initialWidth, initialHeight] =
            presentation::initialWindowSize(*presentationOptions);
        SDL_Window* window = SDL_CreateWindow(
            "Monopoly Modern",
            initialWidth,
            initialHeight,
            presentation::windowFlags(*presentationOptions)
        );

        if (window == nullptr)
        {
            std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
            SDL_Quit();
            return 1;
        }

        const auto configuredWindow =
            presentation::configureWindow(window, *presentationOptions);
        if (!configuredWindow)
        {
            std::cerr << "Window presentation setup failed: "
                << configuredWindow.error() << '\n';
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        std::cout << "Presentation: "
            << presentation::windowModeName(presentationOptions->windowMode)
            << ", requested "
            << presentation::presentModeName(presentationOptions->presentMode)
            << '\n';

        SDL_StartTextInput(window);

        if (!engine::initialize(window, presentationOptions->presentMode))
        {
            SDL_StopTextInput(window);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        presentation::FrameTelemetry frameTelemetry(window);

        if (!initializeAIProfiles())
        {
            engine::shutdown();
            SDL_StopTextInput(window);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        if (game::startup())
        {
            bool finished = false;

            // A native Modern session replaces the old DirectPlay selector.
            // Command-line endpoints configure joining; the menu defaults to hosting.
            messaging::setNetworkStarter([host = networkRequested ? networkHost : true,
                address = networkRequested ? networkAddress : std::string("0.0.0.0"),
                port = networkRequested ? networkPort : std::uint16_t{28799}]()
            {
                auto transport = messaging::openTcpTransport(host, address, port,
                    messaging::TcpSessionMode::Gameplay);
                if (!transport)
                {
                    std::cerr << "Game network startup failed: " << transport.error() << '\n';
                    return false;
                }
                return messaging::startNetwork(std::move(*transport));
            });
            if (networkRequested)
            {
                auto transport = messaging::openTcpTransport(
                    networkHost, networkAddress, networkPort,
                    gameplayRequested ? messaging::TcpSessionMode::Gameplay :
                        messaging::TcpSessionMode::VoiceSpectators);
                if (!transport)
                {
                    std::cerr << "Voice network startup failed: " << transport.error() << '\n';
                    finished = true;
                    result = 1;
                }
                else if (!messaging::startNetwork(std::move(*transport)))
                {
                    std::cerr << "Voice network owner could not be installed.\n";
                    finished = true;
                    result = 1;
                }
            }

            // Voice host/connect is not a lobby launch. Only a real lobby
            // owner may request the historical opening-movie bypass.
            if (!finished) engine::startOpeningMovies(false);

            while (!finished)
            {
                SDL_Event event{};

                while (SDL_PollEvent(&event))
                {
                    if (event.type == SDL_EVENT_QUIT)
                    {
                        uimsg::send({ uimsg::Type::Quit });
                    }
                    else if (
                        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                        event.button.button == SDL_BUTTON_LEFT)
                    {
                        sendMouseMessage(
                            window,
                            uimsg::Type::MouseLeftDown,
                            event.button.x,
                            event.button.y
                        );
                    }
                    else if (
                        event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                        event.button.button == SDL_BUTTON_LEFT)
                    {
                        sendMouseMessage(
                            window,
                            uimsg::Type::MouseLeftUp,
                            event.button.x,
                            event.button.y
                        );
                    }
                    else if (
                        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                        event.button.button == SDL_BUTTON_MIDDLE)
                    {
                        sendMouseMessage(
                            window,
                            uimsg::Type::MouseMiddleDown,
                            event.button.x,
                            event.button.y
                        );
                    }
                    else if (
                        event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                        event.button.button == SDL_BUTTON_MIDDLE)
                    {
                        sendMouseMessage(
                            window,
                            uimsg::Type::MouseMiddleUp,
                            event.button.x,
                            event.button.y
                        );
                    }
                    else if (
                        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                        event.button.button == SDL_BUTTON_RIGHT)
                    {
                        sendMouseMessage(
                            window,
                            uimsg::Type::MouseRightDown,
                            event.button.x,
                            event.button.y
                        );
                    }
                    else if (
                        event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                        event.button.button == SDL_BUTTON_RIGHT)
                    {
                        sendMouseMessage(
                            window,
                            uimsg::Type::MouseRightUp,
                            event.button.x,
                            event.button.y
                        );
                    }
                    else if (event.type == SDL_EVENT_MOUSE_MOTION)
                    {
                        sendMouseMessage(
                            window,
                            uimsg::Type::MouseMoved,
                            event.motion.x,
                            event.motion.y,
                            event.motion.xrel,
                            event.motion.yrel
                        );
                    }
                    else if (event.type == SDL_EVENT_KEY_DOWN)
                    {
                        if (event.key.scancode == SDL_SCANCODE_F11 &&
                            !event.key.repeat)
                        {
                            const auto toggled =
                                presentation::toggleBorderlessFullscreen(
                                    window, *presentationOptions);
                            if (!toggled)
                                std::cerr << "Fullscreen toggle failed: "
                                    << toggled.error() << '\n';
                        }
                        else if (event.key.scancode != SDL_SCANCODE_ESCAPE || !event.key.repeat)
                        {
                            // A held Escape must not activate a newly opened menu after skipping a movie.
                            uimsg::send(
                                {
                                    uimsg::Type::KeyboardPressed,
                                    static_cast<std::int64_t>(
                                        event.key.scancode
                                    )
                                }
                            );
                        }
                    }
                    else if (event.type == SDL_EVENT_KEY_UP)
                    {
                        if (event.key.scancode != SDL_SCANCODE_F11)
                            uimsg::send(
                                {
                                    uimsg::Type::KeyboardReleased,
                                    static_cast<std::int64_t>(
                                        event.key.scancode
                                    )
                                }
                            );
                    }
                    else if (event.type == SDL_EVENT_TEXT_INPUT)
                    {
                        uimsg::Message textMessage{};

                        textMessage.type =
                            uimsg::Type::TextInput;

                        if (event.text.text != nullptr)
                        {
                            textMessage.text =
                                event.text.text;
                        }

                        uimsg::send(textMessage);
                    }
                }

                if (!finished)
                {
                    if (game::updateCycle())
                    {
                        // UDChat switches from the ArtLib TAB_pointer to the
                        // native I-beam only after its input state is updated.
                        mouse::updateChatCursor(chat::stateReadOnly());
                        if (!engine::runCyclicFunctions())
                        {
                            std::cerr
                                << "GPU frame submission failed: "
                                << SDL_GetError()
                                << '\n';

                            finished = true;
                            result = 1;
                        }
                        else
                        {
                            frameTelemetry.framePresented();
                        }
                    }
                    else
                    {
                        finished = true;
                    }
                }
            }

            // STOP is queued before the transport is destroyed. The final pump
            // is best-effort; peers also clean up on EOF/heartbeat timeout.
            engine::stopVoiceChat();
            messaging::pumpNetwork();
            game::shutdown();
        }
        else
        {
            std::cerr << "Game startup failed.\n";
            result = 1;
        }

        engine::shutdown();

        SDL_StopTextInput(window);
        SDL_DestroyWindow(window);
        SDL_Quit();

        return result;
    }
}


