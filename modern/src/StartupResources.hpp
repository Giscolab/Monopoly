#pragma once

#include <expected>
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
        bool checkOnly{};
        std::vector<std::string_view> remaining;
    };

    // Resource switches can be combined with the existing network arguments.
    [[nodiscard]] std::expected<ResourceArguments, std::string>
        parseResourceArguments(std::span<const std::string_view> arguments);

    // Uses SDL's process-local environment; never writes into the installation.
    [[nodiscard]] std::expected<void, std::string>
        selectResourceRoot(std::string_view utf8Root);

    enum class ResourceSetupResult { Ready, Cancelled, Failed };

    // Interactive mode requires SDL video initialized, before creating GPU or
    // game owners. Check-only mode neither opens a window nor starts the game.
    [[nodiscard]] ResourceSetupResult prepareResources(bool interactive);
}
