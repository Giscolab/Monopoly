#include "Application.hpp"
#include "Engine.hpp"
#include "Game.hpp"
#include "LogicalViewport.hpp"
#include "UIMessages.hpp"

#include <SDL3/SDL.h>

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


    void sendMouseMessage(
        SDL_Window* window,
        monopoly::uimsg::Type type,
        float x,
        float y)
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

        (void)monopoly::uimsg::send(
            {
                type,
                static_cast<std::int64_t>(point->x),
                static_cast<std::int64_t>(point->y)
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
                    else if (event.type == SDL_EVENT_MOUSE_MOTION)
                    {
                        sendMouseMessage(
                            window,
                            uimsg::Type::MouseMoved,
                            event.motion.x,
                            event.motion.y
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


