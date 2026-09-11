#include "Application.hpp"
#include "AIMessageIngress.hpp"
#include "Engine.hpp"
#include "Game.hpp"
#include "LogicalViewport.hpp"
#include "UIMessages.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>

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

        if (!point.has_value())
        {
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
    int Application::run()
    {
        int result = 0;

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


