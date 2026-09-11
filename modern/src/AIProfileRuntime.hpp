#pragma once

#include "AIProfile.hpp"

#include <array>
#include <expected>
#include <filesystem>

namespace monopoly::ai::profile
{
    struct RuntimeState
    {
        std::filesystem::path directory{};
        ProfileSet players{};
        std::array<bool, rules::MaxPlayers> playerLoaded{};
        std::array<std::uint8_t, rules::MaxPlayers> loadedToken{};
        std::array<std::uint8_t, rules::MaxPlayers> loadedLevel{};
        Profile expert{};
        bool expertLoaded{};
    };

    void resetRuntime(RuntimeState& state) noexcept;

    [[nodiscard]] std::expected<void, Error> initializeRuntime(
        RuntimeState& state,
        const std::filesystem::path& directory);

    [[nodiscard]] std::expected<void, Error> loadPlayerRuntime(
        RuntimeState& state,
        rules::PlayerNumber player,
        std::uint8_t token,
        std::uint8_t level);

    void clearPlayerRuntime(
        RuntimeState& state,
        rules::PlayerNumber player) noexcept;

    [[nodiscard]] const Profile* playerRuntimeProfile(
        const RuntimeState& state,
        rules::PlayerNumber player) noexcept;

    [[nodiscard]] const Profile* expertRuntimeProfile(
        const RuntimeState& state) noexcept;
}
