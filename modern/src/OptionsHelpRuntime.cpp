#include "OptionsHelpRuntime.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <thread>
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
#include <shellapi.h>
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

#ifndef _WIN32
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

#endif
#ifdef _WIN32
        struct BrowserDispatch
        {
            std::atomic<bool> completed{false};
            std::string error;
        };
        // Main-thread admission guard. Workers own only their result and path;
        // cancellation/shutdown never joins a potentially stuck shell extension.
        std::shared_ptr<BrowserDispatch> outstandingBrowserDispatch;

        std::expected<void, std::string> openNativeDocument(const std::filesystem::path& document)
        {
            // Microsoft ShellExecuteExW requires a COM apartment for shell verbs.
            // NOASYNC keeps the worker apartment alive until dispatch completes.
            const auto initialized = CoInitializeEx(nullptr,
                COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            if (FAILED(initialized))
                return std::unexpected("Cannot initialize Full Help browser dispatch: " +
                    std::to_string(static_cast<unsigned long>(initialized)));
            SHELLEXECUTEINFOW request{};
            request.cbSize = sizeof(request);
            request.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
            request.lpVerb = L"open";
            request.lpFile = document.c_str();
            request.nShow = SW_SHOWNORMAL;
            const bool opened = ShellExecuteExW(&request) != FALSE;
            const auto error = opened ? ERROR_SUCCESS : GetLastError();
            CoUninitialize();
            if (!opened) return std::unexpected("Cannot open Full Help in the default browser: " +
                std::to_string(error));
            return {};
        }
#endif

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
            std::uint32_t timeoutMilliseconds{};
            std::function<std::expected<void, std::string>(const std::filesystem::path&)> openDocument;
#ifdef _WIN32
            std::shared_ptr<BrowserDispatch> browserDispatch;
#endif
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
#ifdef _WIN32
        if (outstandingBrowserDispatch)
        {
            if (!outstandingBrowserDispatch->completed.load(std::memory_order_acquire))
                return std::unexpected("A previous Full Help browser request is still pending");
            outstandingBrowserDispatch.reset();
        }
#endif
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
        job->timeoutMilliseconds = options.timeoutMilliseconds;
        job->openDocument = options.openDocument;
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

    std::optional<std::string> styleFullHelpHtml(std::string_view html, std::uintmax_t maximumBytes)
    {
        if(html.size()>maximumBytes || html.find('\0')!=std::string_view::npos) return {};
        // Validate complete scalar UTF-8 without changing the export's bytes.
        for(std::size_t i=0;i<html.size();)
        {
            const auto first=static_cast<unsigned char>(html[i++]);
            if(first<0x80) continue;
            unsigned count{},code{},minimum{};
            if(first>=0xC2 && first<=0xDF){count=1;code=first&31;minimum=0x80;}
            else if(first>=0xE0 && first<=0xEF){count=2;code=first&15;minimum=0x800;}
            else if(first>=0xF0 && first<=0xF4){count=3;code=first&7;minimum=0x10000;}
            else return {};
            if(count>html.size()-i) return {};
            while(count--)
            {
                const auto next=static_cast<unsigned char>(html[i++]);
                if((next&0xC0)!=0x80) return {};
                code=(code<<6)|(next&63);
            }
            if(code<minimum || code>0x10FFFF || (code>=0xD800 && code<=0xDFFF)) return {};
        }
        std::string lower(html);
        std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(c>='A' && c<='Z'?c+32:c);});
        const auto head=lower.find("<head>"),endHead=lower.find("</head>"),body=lower.find("<body>");
        if(head==std::string::npos || endHead==std::string::npos || body==std::string::npos || head>=endHead || endHead>=body ||
            lower.find("<!doctype html>")==std::string::npos || lower.find("<html")==std::string::npos || lower.find("</html>")==std::string::npos ||
            lower.find("<meta charset=\"utf-8\">",head)>=endHead || lower.find("<nav>",body)==std::string::npos ||
            lower.find("</nav>",body)==std::string::npos || lower.find("<section id=\"topic-",body)==std::string::npos ||
            lower.find("</section>",body)==std::string::npos || lower.find("</body>",body)==std::string::npos ||
            lower.find("monopoly-modern-help")!=std::string::npos) return {};
        constexpr std::string_view theme=R"HELP(
<meta name="viewport" content="width=device-width, initial-scale=1">
<style id="monopoly-modern-help">
html{background:#102e30;color:#203536;color-scheme:light;scroll-behavior:auto}
body{font-family:"Segoe UI",Arial,sans-serif;max-width:72rem;margin:0 auto;padding:clamp(1rem,4vw,3.5rem);line-height:1.65}
body>h1{color:#f4efdf;font-size:clamp(1.7rem,3vw,2.3rem);font-weight:600;letter-spacing:.02em;margin:0 0 1.4rem;padding:.2rem 0 .2rem 1rem;border-left:4px solid #bc9c59}
nav{font-size:.95rem;line-height:1.35;background:#163c3e;border:1px solid #887344;border-radius:14px;padding:clamp(1rem,3vw,2rem);margin-bottom:2rem;box-shadow:0 12px 32px #0003}
nav ul{columns:auto;display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:.05rem 1.5rem;list-style:none;padding:0;margin:0}
nav a{display:block;color:#f1e9d3;padding:.12rem .65rem;border-radius:5px;text-decoration:none;overflow-wrap:anywhere}
nav a:hover{background:#244e4e;color:#fff}
a{color:#215c60;text-underline-offset:.18em;overflow-wrap:anywhere}
a:hover{color:#103d40}
a:focus-visible{outline:3px solid #bc9c59;outline-offset:3px;border-radius:3px}
section{background:#f5f0e2;border:1px solid #c2ad7a;border-radius:14px;padding:clamp(1rem,3vw,2rem);margin-top:2rem;box-shadow:0 12px 32px #0003;scroll-margin-top:1rem;overflow-wrap:anywhere}
section:target{border-color:#d4b367}
.topic-meta{color:#566665;font-size:.85rem}
.nonscroll{background:#ebe3cf;border-left:3px solid #b79754;border-radius:5px;padding:.65rem 1rem}
.f0,.f3,.f4,.f6,.f9{font-family:"Segoe UI",Arial,sans-serif}
.f1,.f2,.f5,.f7,.f8{font-family:"Segoe UI",Arial,sans-serif}
section p{line-height:1.65;margin-block:.65rem!important}
table{max-width:100%;border-collapse:collapse}td{border-color:#baa97f;padding:.5rem .7rem}img{max-width:100%;height:auto}
@media(max-width:640px){nav a{padding:.5rem .65rem}nav ul{grid-template-columns:1fr}body{padding:1rem}section,nav{border-radius:10px;padding:1rem}.f0{font-size:1.6rem}.f3{font-size:1.35rem}}
@media print{html{background:white}body{max-width:none;padding:0}body>h1{color:#203536}nav,section{box-shadow:none;border-color:#999}a{color:inherit}}
</style>
)HELP";
        if(theme.size()>maximumBytes-html.size()) return {};
        std::string result(html);result.insert(endHead,theme);return result;
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
#ifdef _WIN32
        if (job.browserDispatch)
        {
            if (!job.browserDispatch->completed.load(std::memory_order_acquire))
            {
                if (HelpClock::now() >= job.deadline)
                    return fail("Full Help browser dispatch timed out; the Windows request may still complete");
                return false;
            }
            const auto error = job.browserDispatch->error;
            outstandingBrowserDispatch.reset();
            fullHelpJob.reset();
            if (!error.empty()) return std::unexpected(error);
            return true;
        }
#endif
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
        file.close(); // Release the Windows file handle before atomic replacement.
        const auto themed=styleFullHelpHtml(html,job.maximumHtmlBytes);
        std::transform(html.begin(), html.end(), html.begin(), [](unsigned char ch)
        {
            return static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch);
        });
        if (html.find("<html") == std::string::npos || html.find("</html>") == std::string::npos)
            return fail("Full Help exporter produced an incomplete HTML document");
        if(themed)
        {
            auto temporary=job.document;temporary+=".theme.tmp";
            std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
            output.write(themed->data(),static_cast<std::streamsize>(themed->size()));
            output.close();
            std::error_code error;
            if(!output){std::filesystem::remove(temporary,error);return fail("Cannot write the themed Full Help export");}
#ifdef _WIN32
            const bool replaced=MoveFileExW(temporary.c_str(),job.document.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
#else
            std::filesystem::rename(temporary,job.document,error);
            const bool replaced=!error;
#endif
            if(!replaced){std::filesystem::remove(temporary,error);return fail("Cannot publish the themed Full Help export");}
        }
#ifdef _WIN32
        auto dispatch = std::make_shared<BrowserDispatch>();
        try
        {
            // Never use SDL_OpenURL off-thread (SDL_misc.h: main thread only).
            // All captures are owned; this worker cannot touch SDL/game lifetime.
            std::thread([dispatch, document = job.document, opener = job.openDocument]()
            {
                try
                {
                    const auto opened = opener ? opener(document) : openNativeDocument(document);
                    if (!opened) dispatch->error = opened.error();
                }
                catch (const std::exception& error) { dispatch->error = error.what(); }
                catch (...) { dispatch->error = "Full Help browser dispatch failed"; }
                dispatch->completed.store(true, std::memory_order_release);
            }).detach();
        }
        catch (const std::exception& error)
        {
            return fail(std::string("Cannot start Full Help browser dispatch: ") + error.what());
        }
        // Preserve the file even on timeout/cancel: the OS may consume it later.
        job.retainDocument = true;
        job.browserDispatch = dispatch;
        outstandingBrowserDispatch = std::move(dispatch);
        job.deadline = HelpClock::now() + std::chrono::milliseconds(job.timeoutMilliseconds);
        return false;
#else
        const auto url = fileUrl(job.document);
        const auto opened = job.openDocument ? job.openDocument(job.document) :
            (SDL_OpenURL(url.c_str()) ? std::expected<void, std::string>{} :
                std::expected<void, std::string>{std::unexpected(
                    std::string("Cannot open Full Help in the default browser: ") + SDL_GetError())});
        if (!opened) return fail(opened.error());
        job.retainDocument = true;
        fullHelpJob.reset();
        return true;
#endif
    }
}
