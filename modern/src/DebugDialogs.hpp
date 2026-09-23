#pragma once

#include <expected>
#include <string>
#include <string_view>

struct SDL_Window;

namespace monopoly::debugui
{
    enum class Question
    {
        RetryCancel,
        OkCancel,
        YesNo
    };

    struct QuestionPlan
    {
        std::string positiveText;
        std::string negativeText;
        bool positiveDefault{};
        bool negativeDefault{};
        friend bool operator==(
            const QuestionPlan&, const QuestionPlan&) = default;
    };

    [[nodiscard]] QuestionPlan questionPlan(
        Question question, bool defaultPositive = true);

    [[nodiscard]] std::expected<void, std::string> displayMessage(
        std::string_view title,
        std::string_view message,
        SDL_Window* window = nullptr);

    [[nodiscard]] std::expected<bool, std::string> retryMessage(
        std::string_view title,
        std::string_view message,
        SDL_Window* window = nullptr);

    [[nodiscard]] std::expected<bool, std::string> okCancelMessage(
        std::string_view title,
        std::string_view message,
        SDL_Window* window = nullptr);

    [[nodiscard]] std::expected<bool, std::string> yesNoMessage(
        std::string_view title,
        std::string_view message,
        bool defaultYes,
        SDL_Window* window = nullptr);
}
