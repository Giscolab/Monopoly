#pragma once
#include "OptionsUI.hpp"
#include "ResourceRuntime.hpp"
#include <expected>
#include <string>
namespace monopoly::optionsui
{
    [[nodiscard]] std::string helpFileName(data::LanguageId language, bool fullHelp);
    [[nodiscard]] std::expected<void, std::string> openQuickHelp(
        State& state, const data::ResourceSnapshot& resources);
    [[nodiscard]] std::expected<void, std::string> openFullHelp(
        const data::ResourceSnapshot& resources);
}
