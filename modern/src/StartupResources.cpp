#include "StartupResources.hpp"

#include "ResourceRuntime.hpp"
#include "UDUtils.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <iostream>
#include <memory>

namespace monopoly::startup
{
    namespace
    {
        struct FolderResult
        {
            std::atomic<bool> finished{};
            std::string path;
            std::string error;
        };

        void SDLCALL folderSelected(
            void* userdata, const char* const* files, int)
        {
            // SDL can call back from another thread, including after the user
            // requested quit. Keep the result owned until the callback exits.
            std::unique_ptr<std::shared_ptr<FolderResult>> owner(
                static_cast<std::shared_ptr<FolderResult>*>(userdata));
            auto& result = **owner;
            if (!files) result.error = SDL_GetError();
            else if (files[0]) result.path = files[0];
            result.finished.store(true, std::memory_order_release);
        }

        std::expected<std::optional<std::string>, std::string> chooseFolder()
        {
            auto result = std::make_shared<FolderResult>();
            SDL_ShowOpenFolderDialog(folderSelected,
                new std::shared_ptr<FolderResult>(result), nullptr, nullptr, false);
            bool quit = false;
            while (!result->finished.load(std::memory_order_acquire))
            {
                SDL_Event event{};
                if (SDL_WaitEventTimeout(&event, 20) && event.type == SDL_EVENT_QUIT)
                    quit = true;
            }
            if (!result->error.empty()) return std::unexpected(result->error);
            if (quit || result->path.empty()) return std::optional<std::string>{};
            return std::optional<std::string>{result->path};
        }
    }

    std::expected<ResourceArguments, std::string> parseResourceArguments(
        std::span<const std::string_view> arguments)
    {
        ResourceArguments result;
        for (std::size_t index = 0; index < arguments.size(); ++index)
        {
            const auto argument = arguments[index];
            if (argument == "--data-root")
            {
                if (result.dataRoot || index + 1 == arguments.size() ||
                    arguments[index + 1].empty() ||
                    arguments[index + 1].starts_with("--"))
                    return std::unexpected(
                        "--data-root requires one absolute installation folder");
                result.dataRoot = arguments[++index];
            }
            else if (argument == "--check-resources")
            {
                if (result.checkOnly)
                    return std::unexpected("--check-resources was specified twice");
                result.checkOnly = true;
            }
            else result.remaining.push_back(argument);
        }
        return result;
    }

    std::expected<void, std::string> selectResourceRoot(std::string_view utf8Root)
    {
        const std::filesystem::path root{
            std::u8string(utf8Root.begin(), utf8Root.end())};
        const auto paths = data::ResourcePaths::create(std::array{root});
        if (!paths) return std::unexpected(paths.error().detail);
        auto* environment = SDL_GetEnvironment();
        const std::string value(utf8Root);
        if (!environment || !SDL_SetEnvironmentVariable(
            environment, "MONOPOLY_DATA_ROOT", value.c_str(), true))
            return std::unexpected(std::string(SDL_GetError()));
        return {};
    }

    ResourceSetupResult prepareResources(bool interactive)
    {
        for (;;)
        {
            std::string details;
            if (!udutils::generateINIFile())
                details = "The configured installation folder is invalid.\n";
            else
            {
                const auto* paths = udutils::resourcePaths();
                const auto issues = data::inspectResourceInstallation(*paths);
                const auto root = paths->roots().front().u8string();
                std::cout << "Resource folder: "
                    << std::string(root.begin(), root.end()) << '\n';
                if (issues.empty())
                {
                    std::cout << "All eight required banks and the language catalog "
                        "can be opened. Gameplay assets remain to be exercised.\n";
                    return ResourceSetupResult::Ready;
                }
                for (const auto& issue : issues)
                {
                    std::cerr << '[' << data::dataErrorCodeName(issue.code)
                        << "] " << issue.detail << '\n';
                    if (issue.code == data::DataErrorCode::ResourceNotFound)
                        details += "Missing: ";
                    else details += "Cannot read: ";
                    // Bank errors start with the expected relative filename;
                    // detailed parser diagnostics remain available in stderr.
                    details += issue.detail.substr(0, issue.detail.find(": "));
                    details += '\n';
                }
            }
            if (!interactive) return ResourceSetupResult::Failed;

            const std::string message =
                "Choose the game installation folder containing Dat_Mon.\n\n" +
                details + "\nThe original game files are required to start.";
            const std::array<SDL_MessageBoxButtonData, 2> buttons{{
                {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose folder"},
                {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit"}
            }};
            const SDL_MessageBoxData dialog{SDL_MESSAGEBOX_WARNING, nullptr,
                "Monopoly - Game files", message.c_str(),
                static_cast<int>(buttons.size()), buttons.data(), nullptr};
            int choice = 0;
            if (!SDL_ShowMessageBox(&dialog, &choice))
            {
                std::cerr << "Resource setup dialog failed: " << SDL_GetError() << '\n';
                return ResourceSetupResult::Failed;
            }
            if (choice != 1) return ResourceSetupResult::Cancelled;
            const auto folder = chooseFolder();
            if (!folder)
            {
                std::cerr << "Folder selection failed: " << folder.error() << '\n';
                (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                    "Monopoly - Folder selection", folder.error().c_str(), nullptr);
                return ResourceSetupResult::Failed;
            }
            if (!*folder) return ResourceSetupResult::Cancelled;
            const auto selected = selectResourceRoot(**folder);
            if (!selected)
            {
                std::cerr << "Selected folder is invalid: " << selected.error() << '\n';
                (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                    "Monopoly - Game files", "The selected folder cannot be used.", nullptr);
            }
        }
    }
}
