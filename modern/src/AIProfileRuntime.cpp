#include "AIProfileRuntime.hpp"

#include <utility>

namespace monopoly::ai::profile
{
    void resetRuntime(RuntimeState& state) noexcept
    {
        state = {};
    }

    std::expected<void, Error> initializeRuntime(
        RuntimeState& state,
        const std::filesystem::path& directory)
    {
        auto expert = load(directory / "Normal.ai", 3);
        if (!expert)
            return std::unexpected(expert.error());

        RuntimeState next{};
        next.directory = directory;
        next.expert = std::move(*expert);
        next.expertLoaded = true;
        state = std::move(next);
        return {};
    }

    std::expected<void, Error> loadPlayerRuntime(
        RuntimeState& state,
        rules::PlayerNumber player,
        std::uint8_t token,
        std::uint8_t level)
    {
        if (!state.expertLoaded || state.directory.empty())
        {
            return std::unexpected(Error{
                ErrorCode::IoError,
                0,
                "AI profile runtime is not initialized"});
        }
        if (player >= rules::MaxPlayers)
        {
            return std::unexpected(Error{
                ErrorCode::InvalidEnum,
                0,
                "AI player slot is out of range"});
        }

        const auto fileName = tokenFileName(token);
        if (fileName.empty())
        {
            return std::unexpected(Error{
                ErrorCode::InvalidEnum,
                0,
                "AI token has no retail profile"});
        }

        if (level < 1 || level > 3)
        {
            return std::unexpected(Error{
                ErrorCode::InvalidLevel,
                0,
                "AI level must be 1..3"});
        }

        if (state.playerLoaded[player] &&
            state.loadedToken[player] == token &&
            state.loadedLevel[player] == level)
        {
            return {};
        }

        auto loaded = load(state.directory / fileName, level);
        if (!loaded)
            return std::unexpected(loaded.error());

        state.players[player] = std::move(*loaded);
        state.playerLoaded[player] = true;
        state.loadedToken[player] = token;
        state.loadedLevel[player] = level;
        return {};
    }

    void clearPlayerRuntime(
        RuntimeState& state,
        rules::PlayerNumber player) noexcept
    {
        if (player >= rules::MaxPlayers)
            return;
        state.players[player] = {};
        state.playerLoaded[player] = false;
        state.loadedToken[player] = 0;
        state.loadedLevel[player] = 0;
    }

    const Profile* playerRuntimeProfile(
        const RuntimeState& state,
        rules::PlayerNumber player) noexcept
    {
        if (player >= rules::MaxPlayers || !state.playerLoaded[player])
            return nullptr;
        return &state.players[player];
    }

    const Profile* expertRuntimeProfile(
        const RuntimeState& state) noexcept
    {
        return state.expertLoaded ? &state.expert : nullptr;
    }
}
