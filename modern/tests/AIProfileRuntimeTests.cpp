#include "AIProfileRuntime.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

#ifndef MONOPOLY_LEGACY_SOURCE_DIR
#error MONOPOLY_LEGACY_SOURCE_DIR is required
#endif

namespace
{
    using namespace monopoly;

    void require(bool condition, const char* description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << description << '\n';
        if (!condition)
            throw std::runtime_error(description);
    }

    std::filesystem::path profileDirectory()
    {
        return std::filesystem::path(MONOPOLY_LEGACY_SOURCE_DIR) /
            "monopoly";
    }

    void testExpertAndPlayerLoading()
    {
        ai::profile::RuntimeState runtime{};
        const auto initialized = ai::profile::initializeRuntime(
            runtime, profileDirectory());
        require(initialized.has_value(),
            "AI runtime loads retail expert profile");
        require(ai::profile::expertRuntimeProfile(runtime) != nullptr,
            "AI runtime exposes expert profile after initialization");
        require(runtime.expertLoaded &&
                runtime.expert.minCashOnHand == 130,
            "expert profile is Normal.ai level 3");

        auto loaded = ai::profile::loadPlayerRuntime(runtime, 0, 0, 3);
        require(loaded.has_value(),
            "AI runtime loads Cannon profile for token 0");
        require(runtime.playerLoaded[0] &&
                runtime.loadedToken[0] == 0 &&
                runtime.loadedLevel[0] == 3,
            "AI runtime records player token and level");
        require(ai::profile::playerRuntimeProfile(runtime, 0) != nullptr &&
                runtime.players[0].tradeCounterLimit == 3,
            "AI runtime publishes Cannon level 3 values");
    }

    void testFailuresAreContained()
    {
        ai::profile::RuntimeState runtime{};
        require(ai::profile::initializeRuntime(
                    runtime, profileDirectory()).has_value(),
            "AI runtime fixture initializes");
        require(ai::profile::loadPlayerRuntime(
                    runtime, 1, 10, 1).has_value(),
            "AI runtime loads Moneybag profile");

        const auto oldDirectory = runtime.directory;
        const auto oldExpertCash = runtime.expert.minCashOnHand;
        const auto failedInit = ai::profile::initializeRuntime(
            runtime, oldDirectory / "missing-directory");
        require(!failedInit && runtime.directory == oldDirectory &&
                runtime.expert.minCashOnHand == oldExpertCash,
            "failed expert reload leaves previous runtime published");

        const auto oldCounterLimit = runtime.players[1].tradeCounterLimit;
        const auto badToken = ai::profile::loadPlayerRuntime(
            runtime, 1, 99, 2);
        require(!badToken && runtime.playerLoaded[1] &&
                runtime.loadedToken[1] == 10 &&
                runtime.players[1].tradeCounterLimit == oldCounterLimit,
            "invalid token cannot corrupt loaded player profile");
    }

    void testClearAndValidation()
    {
        ai::profile::RuntimeState runtime{};
        const auto notInitialized = ai::profile::loadPlayerRuntime(
            runtime, 0, 0, 1);
        require(!notInitialized,
            "AI runtime rejects player load before expert initialization");

        require(ai::profile::initializeRuntime(
                    runtime, profileDirectory()).has_value(),
            "AI runtime validation fixture initializes");
        require(!ai::profile::loadPlayerRuntime(
                    runtime, rules::MaxPlayers, 0, 1),
            "AI runtime rejects out-of-range player slot");
        require(!ai::profile::loadPlayerRuntime(runtime, 0, 0, 0),
            "AI runtime rejects retail level zero");

        require(ai::profile::loadPlayerRuntime(runtime, 0, 0, 1).has_value(),
            "AI runtime loads player before clear");
        ai::profile::clearPlayerRuntime(runtime, 0);
        require(!runtime.playerLoaded[0] &&
                ai::profile::playerRuntimeProfile(runtime, 0) == nullptr,
            "AI runtime clears one player without clearing expert");
        require(ai::profile::expertRuntimeProfile(runtime) != nullptr,
            "clearing a player preserves expert profile");
    }
}

int main()
{
    try
    {
        testExpertAndPlayerLoading();
        testFailuresAreContained();
        testClearAndValidation();
        std::cout << "All AI profile runtime tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
