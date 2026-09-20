#include "OptionsHelpRuntime.hpp"
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
#ifdef _WIN32
        // UDOpts uses mbstowcs on the local Windows code page, not UTF-8.
        if (!text.empty())
        {
            const int count = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (count <= 0) return std::unexpected("cannot decode retail quick-help text");
            std::wstring wide(static_cast<std::size_t>(count), L'\0');
            MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), wide.data(), count);
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), count, nullptr, 0, nullptr, nullptr);
            if (bytes <= 0) return std::unexpected("cannot encode retail quick-help text");
            text.resize(static_cast<std::size_t>(bytes));
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), count, text.data(), bytes, nullptr, nullptr);
        }
#else
        for (const unsigned char c : text)
            if (c >= 128) return std::unexpected("retail quick-help local code page requires a platform decoder");
#endif
        state.quickHelpText = std::move(text);
        state.quickHelpLines.clear();
        state.quickHelpFirstLine = 0;
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
