#include "OptionsHelpRuntime.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace monopoly::optionsui
{
    std::expected<std::string, std::string> decodeQuickHelpText(std::string_view bytes)
    {
        // UDOpts reads qhelp files as bytes and converts them with mbstowcs.
        // Their ten western-language assets are not UTF-8 (except ASCII 01/02).
        // Use deterministic CP1252 on every host instead of its current locale.
        constexpr std::array<std::uint16_t, 32> highControls{
            0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
            0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
            0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
            0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};
        std::string result;
        result.reserve(bytes.size());
        for (const unsigned char byte : bytes)
        {
            if (byte == 0)
                return std::unexpected("retail quick-help text contains an embedded NUL");
            const std::uint16_t code = byte >= 0x80 && byte <= 0x9F
                ? highControls[byte - 0x80] : byte;
            if (code == 0)
                return std::unexpected("retail quick-help text contains an undefined Windows-1252 byte");
            if (code < 0x80) result.push_back(static_cast<char>(code));
            else if (code < 0x800)
            {
                result.push_back(static_cast<char>(0xC0 | (code >> 6)));
                result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            else
            {
                result.push_back(static_cast<char>(0xE0 | (code >> 12)));
                result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
        }
        return result;
    }

    std::string helpFileName(data::LanguageId language, bool fullHelp)
    {
        char name[32]{};
        std::snprintf(name, sizeof(name), fullHelp ? "mono%02u.hlp" : "qhelp%02u.txt",
            static_cast<unsigned>(language));
        return name;
    }

    std::expected<void, std::string> openQuickHelp(
        State& state, const data::ResourceSnapshot& resources)
    {
        const auto path = resources.paths().resolve(helpFileName(resources.context().language, false));
        if (!path) return std::unexpected(path.error().detail);
        std::ifstream file(*path, std::ios::binary);
        if (!file) return std::unexpected("cannot open retail quick-help file");
        std::string text((std::istreambuf_iterator<char>(file)), {});
        if (file.bad()) return std::unexpected("cannot read retail quick-help file");
        auto decoded = decodeQuickHelpText(text);
        if (!decoded) return std::unexpected(decoded.error());
        state.quickHelpText = std::move(*decoded);
        state.quickHelpLines.clear();
        state.quickHelpFirstLine = 0;
        state.quickHelpPageOffsets.clear();
        state.quickHelpPageIndex = 0;
        state.quickHelpInitialPage = true;
        state.quickHelpVisible = true;
        return {};
    }

    std::expected<void, std::string> openFullHelp(const data::ResourceSnapshot& resources)
    {
        const auto path = resources.paths().resolve(helpFileName(resources.context().language, true));
        if (!path) return std::unexpected(path.error().detail);
#ifdef _WIN32
        if (!WinHelpW(nullptr, path->c_str(), HELP_CONTENTS, 0))
            return std::unexpected("Windows could not open the retail WinHelp contents");
        return {};
#else
        return std::unexpected("retail full help requires a WinHelp viewer on this platform");
#endif
    }
}
