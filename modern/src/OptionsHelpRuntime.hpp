#pragma once
#include "OptionsUI.hpp"
#include "ResourceRuntime.hpp"
#include <expected>
#include <string>
#include <string_view>
namespace monopoly::optionsui
{
    [[nodiscard]] std::string helpFileName(data::LanguageId language, bool fullHelp);
    // The supplied western qhelp01..10 files use single-byte text. CP1252 is
    // a portable choice compatible with every byte present in that corpus;
    // those files alone do not distinguish CP1252 from ISO-8859-1.
    [[nodiscard]] std::expected<std::string, std::string> decodeQuickHelpText(
        std::string_view bytes);
    [[nodiscard]] std::expected<void, std::string> openQuickHelp(
        State& state, const data::ResourceSnapshot& resources);
    [[nodiscard]] std::expected<void, std::string> openFullHelp(
        const data::ResourceSnapshot& resources);
}
