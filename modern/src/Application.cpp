#include "Application.hpp"
#include "AIMessageIngress.hpp"
#include "Engine.hpp"
#include "Game.hpp"
#include "LogicalViewport.hpp"
#include "Messaging.hpp"
#include "MousePointer.hpp"
#include "ChatRuntime.hpp"
#include "TcpMessageTransport.hpp"
#include "UIMessages.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace
{
    [[nodiscard]] std::optional<
        monopoly::logicalviewport::LogicalPoint>
    mouseToLogical(
        SDL_Window* window,
        float x,
        float y)
    {
        int width = 0;
        int height = 0;

        if (!SDL_GetWindowSize(window, &width, &height))
        {
            return std::nullopt;
        }

        return monopoly::logicalviewport::windowToLogical(
            monopoly::logicalviewport::makeTransform(width, height),
            static_cast<double>(x),
            static_cast<double>(y)
        );
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

        monopoly::mouse::updatePosition(
            static_cast<int>(point->x), static_cast<int>(point->y), true);

        int width = 0;
        int height = 0;
        std::int64_t logicalDeltaX = 0;
        std::int64_t logicalDeltaY = 0;
        if (SDL_GetWindowSize(window, &width, &height))
        {
            const auto transform = monopoly::logicalviewport::makeTransform(width, height);
            if (transform.valid())
            {
                logicalDeltaX = static_cast<std::int64_t>(
                    std::lround(static_cast<double>(deltaX) / transform.scale));
                logicalDeltaY = static_cast<std::int64_t>(
                    std::lround(static_cast<double>(deltaY) / transform.scale));
            }
        }
        const auto modifier = (SDL_GetModState() & SDL_KMOD_CTRL) != 0
            ? monopoly::uimsg::MouseModifierControl : 0;
        (void)monopoly::uimsg::send(
            {
                type,
                static_cast<std::int64_t>(point->x),
                static_cast<std::int64_t>(point->y),
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

        bool networkRequested = false;
        bool networkHost = false;
        bool gameplayRequested = false;
        std::string networkAddress;
        std::uint16_t networkPort = 0;
        if (argc > 1)
        {
            const std::string_view mode(argv[1]);
            const auto usage = []
            {
                std::cerr << "Usage: MonopolyModern [--voice-host IPv4:port | "
                    "--voice-connect IPv4:port | --network-host IPv4:port | "
                    "--network-connect IPv4:port]\n"
                    "Network menu without arguments hosts on 0.0.0.0:28799.\n";
            };
            if (argc != 3 || (mode != "--voice-host" && mode != "--voice-connect" &&
                mode != "--network-host" && mode != "--network-connect"))
            {
                usage();
                return 1;
            }
            const std::string_view endpoint(argv[2]);
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

        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
            return 1;
        }

        SDL_Window* window = SDL_CreateWindow(
            "Monopoly Modern",
            800,
            600,
            SDL_WINDOW_RESIZABLE |
                SDL_WINDOW_HIGH_PIXEL_DENSITY
        );

        if (window == nullptr)
        {
            std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
            SDL_Quit();
            return 1;
        }

        SDL_StartTextInput(window);

        if (!engine::initialize(window))
        {
            SDL_StopTextInput(window);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

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
                        uimsg::send(
                            {
                                uimsg::Type::KeyboardPressed,
                                static_cast<std::int64_t>(
                                    event.key.scancode
                                )
                            }
                        );
                    }
                    else if (event.type == SDL_EVENT_KEY_UP)
                    {
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


