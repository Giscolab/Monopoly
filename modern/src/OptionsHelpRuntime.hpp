#pragma once
#include "OptionsUI.hpp"
#include "ResourceRuntime.hpp"
#include <expected>
#include <cstdint>
#include <filesystem>
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
    struct FullHelpOptions
    {
        // An executable path, never a shell command. Empty uses MONOPOLY_WINHLP
        // when defined, otherwise the winhlp executable available on PATH.
        std::string exporterExecutable;
        std::uint32_t timeoutMilliseconds{30000};
        std::uintmax_t maximumHtmlBytes{32 * 1024 * 1024};
    };

    // Starts a nonblocking HLP -> self-contained HTML export in the user's
    // preference directory. Original resources are opened read-only.
    [[nodiscard]] std::expected<void, std::string> openFullHelp(
        const data::ResourceSnapshot& resources, const FullHelpOptions& options = {});
    // Called on the application thread: false = idle/pending, true = a completed
    // document was handed to the default browser. Errors are consumed once.
    [[nodiscard]] std::expected<bool, std::string> pollFullHelp();
    [[nodiscard]] bool fullHelpPending() noexcept;
    void cancelFullHelp() noexcept;
}
