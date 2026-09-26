#include "OptionsHelpRuntime.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace monopoly::optionsui
{
    namespace
    {
        using HelpClock = std::chrono::steady_clock;

        std::string pathUtf8(const std::filesystem::path& path)
        {
            const auto text = path.u8string();
            return {reinterpret_cast<const char*>(text.data()), text.size()};
        }

        std::string fileUrl(const std::filesystem::path& path)
        {
            const auto generic = path.generic_u8string();
            const std::string text(reinterpret_cast<const char*>(generic.data()), generic.size());
            std::string url = text.starts_with("//") ? "file:" :
                text.starts_with('/') ? "file://" : "file:///";
            constexpr char hex[] = "0123456789ABCDEF";
            for (const unsigned char byte : text)
            {
                if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                    (byte >= '0' && byte <= '9') || byte == '/' || byte == ':' ||
                    byte == '-' || byte == '_' || byte == '.' || byte == '~')
                    url.push_back(static_cast<char>(byte));
                else
                {
                    url.push_back('%');
                    url.push_back(hex[byte >> 4]);
                    url.push_back(hex[byte & 15]);
                }
            }
            return url;
        }

        struct FullHelpJob
        {
            SDL_Process* process{};
            SDL_IOStream* errors{};
#ifdef _WIN32
            HANDLE statusHandle{};
#endif
            std::filesystem::path directory;
            std::filesystem::path document;
            HelpClock::time_point deadline;
            std::uintmax_t maximumHtmlBytes{};
            std::string diagnostics;
            bool exited{};
            bool retainDocument{};

            ~FullHelpJob()
            {
                if (process)
                {
                    if (!exited)
                    {
                        SDL_KillProcess(process, true);
                        SDL_WaitProcess(process, true, nullptr);
                    }
#ifdef _WIN32
                    if (statusHandle) CloseHandle(statusHandle);
#endif
                    SDL_DestroyProcess(process);
                }
                // Delete only these two paths created by this job; never recurse
                // over a resource root, the preference directory or other exports.
                if (!retainDocument && !directory.empty())
                {
                    std::error_code ignored;
                    std::filesystem::remove(document, ignored);
                    std::filesystem::remove(directory, ignored);
                }
            }

            std::expected<void, std::string> drainErrors()
            {
                std::array<char, 2048> buffer{};
                for (int read = 0; read < 8; ++read)
                {
                    const auto bytes = SDL_ReadIO(errors, buffer.data(), buffer.size());
                    diagnostics.append(buffer.data(),
                        std::min(bytes, std::size_t{16384} - diagnostics.size()));
                    if (SDL_GetIOStatus(errors) == SDL_IO_STATUS_ERROR)
                        return std::unexpected(std::string("Full Help exporter pipe: ") + SDL_GetError());
                    if (bytes == 0) break;
                }
                return {};
            }
        };

        std::unique_ptr<FullHelpJob> fullHelpJob;
        std::uint64_t exportSerial{};

        std::expected<void, std::string> createExportDirectory(FullHelpJob& job)
        {
            char* raw = SDL_GetPrefPath("Giscolab", "Monopoly");
            if (!raw)
                return std::unexpected(std::string("Full Help cache directory: ") + SDL_GetError());
            const auto root = std::filesystem::u8path(raw) / "full-help";
            SDL_free(raw);
            std::error_code error;
            std::filesystem::create_directories(root, error);
            if (error) return std::unexpected("Cannot create Full Help cache: " + error.message());
            const auto stamp = HelpClock::now().time_since_epoch().count();
            for (unsigned attempt = 0; attempt < 100; ++attempt)
            {
                const auto candidate = root /
                    ("export-" + std::to_string(stamp) + "-" + std::to_string(++exportSerial));
                if (std::filesystem::create_directory(candidate, error))
                {
                    job.directory = candidate;
                    job.document = candidate / "contents.html";
                    return {};
                }
                if (error && error != std::errc::file_exists)
                    return std::unexpected("Cannot create Full Help export: " + error.message());
                error.clear();
            }
            return std::unexpected("Cannot allocate a unique Full Help export directory");
        }
    }

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

    std::expected<void, std::string> openFullHelp(
        const data::ResourceSnapshot& resources, const FullHelpOptions& options)
    {
        if (fullHelpJob) return std::unexpected("A Full Help export is already running");
        if (!options.timeoutMilliseconds || options.maximumHtmlBytes < 32)
            return std::unexpected("Invalid Full Help export limits");
        const auto path = resources.paths().resolve(helpFileName(resources.context().language, true));
        if (!path) return std::unexpected(path.error().detail);

        // Reject missing/truncated/non-HLP inputs before invoking any executable.
        std::ifstream input(*path, std::ios::binary);
        std::array<unsigned char, 4> signature{};
        input.read(reinterpret_cast<char*>(signature.data()), signature.size());
        if (!input || signature != std::array<unsigned char, 4>{0x3F, 0x5F, 0x03, 0x00})
            return std::unexpected("Full Help resource is not a readable WinHelp HLP file");

        auto job = std::make_unique<FullHelpJob>();
        auto created = createExportDirectory(*job);
        if (!created) return created;
        job->deadline = HelpClock::now() + std::chrono::milliseconds(options.timeoutMilliseconds);
        job->maximumHtmlBytes = options.maximumHtmlBytes;
        std::string exporter = options.exporterExecutable;
        if (exporter.empty())
        {
            const auto* configured = SDL_getenv("MONOPOLY_WINHLP");
            exporter = configured && *configured ? configured : "winhlp";
        }
        if (exporter.find('\0') != std::string::npos)
            return std::unexpected("Full Help exporter executable contains an embedded NUL");
        const std::array<std::string, 6> arguments{
            exporter, pathUtf8(*path), "--html", pathUtf8(job->document), "--images", "embed"};
        std::array<const char*, 7> argv{};
        for (std::size_t index = 0; index < arguments.size(); ++index)
            argv[index] = arguments[index].c_str();
        const auto properties = SDL_CreateProperties();
        if (!properties)
            return std::unexpected(std::string("Full Help process properties: ") + SDL_GetError());
        bool configured = SDL_SetPointerProperty(properties,
            SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data()) &&
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL) &&
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL) &&
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_APP);
#ifdef _WIN32
        configured = configured && SDL_SetBooleanProperty(properties,
            SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
#endif
        if (configured) job->process = SDL_CreateProcessWithProperties(properties);
        const std::string processError = job->process ? std::string{} : SDL_GetError();
        SDL_DestroyProperties(properties);
        if (!job->process)
            return std::unexpected("Cannot start the Full Help exporter; install winhlp with HTML/image "
                "support or set MONOPOLY_WINHLP to its executable: " + processError);
#ifdef _WIN32
        // Hidden SDL background processes report a masked exit status. Retain a
        // query handle so an exporter failure cannot be mistaken for success.
        const auto pid = SDL_GetNumberProperty(SDL_GetProcessProperties(job->process),
            SDL_PROP_PROCESS_PID_NUMBER, 0);
        job->statusHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
        if (!job->statusHandle)
            return std::unexpected("Cannot retain Full Help exporter exit status: " +
                std::to_string(GetLastError()));
#endif
        job->errors = static_cast<SDL_IOStream*>(SDL_GetPointerProperty(
            SDL_GetProcessProperties(job->process), SDL_PROP_PROCESS_STDERR_POINTER, nullptr));
        if (!job->errors) return std::unexpected("Full Help exporter diagnostic pipe unavailable");
        fullHelpJob = std::move(job);
        return {};
    }

    bool fullHelpPending() noexcept { return bool(fullHelpJob); }

    void cancelFullHelp() noexcept { fullHelpJob.reset(); }

    std::expected<bool, std::string> pollFullHelp()
    {
        if (!fullHelpJob) return false;
        auto fail = [](std::string message) -> std::expected<bool, std::string>
        {
            fullHelpJob.reset();
            return std::unexpected(std::move(message));
        };
        auto& job = *fullHelpJob;
        if (auto drained = job.drainErrors(); !drained) return fail(drained.error());
        int exitCode{};
        job.exited = SDL_WaitProcess(job.process, false, &exitCode);
        std::error_code sizeError;
        const auto size = std::filesystem::file_size(job.document, sizeError);
        if (!sizeError && size > job.maximumHtmlBytes)
            return fail("Full Help HTML exceeds the configured size limit");
        if (!job.exited)
        {
            if (HelpClock::now() >= job.deadline)
                return fail("Full Help export timed out: " + job.diagnostics);
            return false;
        }
#ifdef _WIN32
        DWORD actualExitCode{};
        if (!GetExitCodeProcess(job.statusHandle, &actualExitCode))
            return fail("Cannot query Full Help exporter exit status: " + std::to_string(GetLastError()));
        exitCode = static_cast<int>(actualExitCode);
#endif
        if (auto drained = job.drainErrors(); !drained) return fail(drained.error());
        if (exitCode != 0)
            return fail("Full Help export failed (exit " + std::to_string(exitCode) + "): " + job.diagnostics);
        if (sizeError || size < 32)
            return fail("Full Help exporter did not produce a readable HTML document");
        // Bounded verification rejects an empty/unfinished output. Topic and image
        // fidelity is qualified against real HLP assets, not inferred from this check.
        std::ifstream file(job.document, std::ios::binary);
        std::string html(static_cast<std::size_t>(size), '\0');
        file.read(html.data(), static_cast<std::streamsize>(html.size()));
        if (!file) return fail("Cannot read the completed Full Help HTML export");
        std::transform(html.begin(), html.end(), html.begin(), [](unsigned char ch)
        {
            return static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch);
        });
        if (html.find("<html") == std::string::npos || html.find("</html>") == std::string::npos)
            return fail("Full Help exporter produced an incomplete HTML document");
        const auto url = fileUrl(job.document);
        if (!SDL_OpenURL(url.c_str()))
            return fail(std::string("Cannot open the Full Help document in the default browser: ") + SDL_GetError());
        job.retainDocument = true;
        fullHelpJob.reset();
        return true;
    }
}
