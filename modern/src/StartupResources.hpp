#pragma once

#include "ResourceRuntime.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace monopoly::startup
{
    struct ResourceArguments
    {
        std::optional<std::string> dataRoot;
        std::optional<std::string> dataOverrides;
        bool checkOnly{};
        data::ResourceContext context{};
        std::vector<std::string_view> remaining;
    };

    // Resource switches can be combined with the existing network arguments.
    [[nodiscard]] std::expected<ResourceArguments, std::string>
        parseResourceArguments(std::span<const std::string_view> arguments);

    // Process-local selection shared by installation qualification and startup.
    [[nodiscard]] std::expected<void, std::string>
        selectResourceContext(data::ResourceContext context);
    [[nodiscard]] data::ResourceContext resourceContext() noexcept;

    // Uses SDL's process-local environment; never writes into the installation.
    [[nodiscard]] std::expected<void, std::string>
        selectResourceRoot(std::string_view utf8Root);

    // Optional manifest of loose logical DATA payloads layered above DAT.
    [[nodiscard]] std::expected<void, std::string>
        selectDataOverrideManifest(std::string_view utf8Path);
    [[nodiscard]] std::optional<std::filesystem::path>
        dataOverrideManifest();

    enum class ResourceSetupResult { Ready, Cancelled, Failed };

    // Interactive mode requires SDL video initialized, before creating GPU or
    // game owners. Check-only mode neither opens a window nor starts the game.
    [[nodiscard]] ResourceSetupResult prepareResources(bool interactive);
}
