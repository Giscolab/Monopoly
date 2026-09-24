#include "DebugDialogs.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdlib>

namespace monopoly::debugui
{
    namespace
    {
        [[nodiscard]] std::string sdlFailure(
            std::string_view operation)
        {
            return std::string(operation) + ": " + SDL_GetError();
        }

        [[nodiscard]] std::expected<bool, std::string> ask(
            std::string_view title,
            std::string_view message,
            const QuestionPlan& plan,
            SDL_Window* window)
        {
            const std::array<SDL_MessageBoxButtonData, 2> buttons{{
                {
                    plan.positiveDefault
                        ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0U,
                    1,
                    plan.positiveText.c_str()
                },
                {
                    plan.negativeDefault
                        ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT |
                          SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT
                        : SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
                    0,
                    plan.negativeText.c_str()
                }
            }};

            const std::string titleText(title);
            const std::string messageText(message);
            SDL_MessageBoxData data{};
            data.flags = SDL_MESSAGEBOX_INFORMATION;
            data.window = window;
            data.title = titleText.c_str();
            data.message = messageText.c_str();
            data.numbuttons = static_cast<int>(buttons.size());
            data.buttons = buttons.data();

            int selected = -1;
            if (!SDL_ShowMessageBox(&data, &selected))
                return std::unexpected(
                    sdlFailure("SDL_ShowMessageBox"));
            return selected == 1;
        }
    }

    QuestionPlan questionPlan(
        Question question, bool defaultPositive)
    {
        QuestionPlan plan{};
        switch (question)
        {
        case Question::RetryCancel:
            plan.positiveText = "Retry";
            plan.negativeText = "Cancel";
            break;
        case Question::OkCancel:
            plan.positiveText = "OK";
            plan.negativeText = "Cancel";
            break;
        case Question::YesNo:
            plan.positiveText = "Yes";
            plan.negativeText = "No";
            break;
        }
        plan.positiveDefault = defaultPositive;
        plan.negativeDefault = !defaultPositive;
        return plan;
    }

    std::expected<void, std::string> displayMessage(
        std::string_view title,
        std::string_view message,
        SDL_Window* window)
    {
        const std::string titleText(
            title.empty() ? std::string_view("Error:") : title);
        const std::string messageText(message);
        if (!SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_INFORMATION,
                titleText.c_str(),
                messageText.c_str(),
                window))
            return std::unexpected(
                sdlFailure("SDL_ShowSimpleMessageBox"));
        return {};
    }

    std::expected<bool, std::string> retryMessage(
        std::string_view title,
        std::string_view message,
        SDL_Window* window)
    {
        return ask(
            title, message,
            questionPlan(Question::RetryCancel, true),
            window);
    }

    std::expected<bool, std::string> okCancelMessage(
        std::string_view title,
        std::string_view message,
        SDL_Window* window)
    {
        return ask(
            title, message,
            questionPlan(Question::OkCancel, true),
            window);
    }

    std::expected<bool, std::string> yesNoMessage(
        std::string_view title,
        std::string_view message,
        bool defaultYes,
        SDL_Window* window)
    {
        return ask(
            title, message,
            questionPlan(Question::YesNo, defaultYes),
            window);
    }

    [[noreturn]] void errorExit(
        std::string_view message,
        SDL_Window* window)
    {
        (void)displayMessage(
            "Something went wrong!  Error Exit Message:",
            message,
            window);
        std::exit(ErrorExitStatus);
    }
}
