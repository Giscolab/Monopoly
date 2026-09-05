#include "UDUtils.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace
{
    int failures{};
    constexpr const char* RootVariable = "MONOPOLY_DATA_ROOT";

    void expect(bool condition, const char* description)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "[FAIL] " << description << '\n';
        }
    }

    std::string utf8(const std::filesystem::path& path)
    {
        auto value = path.u8string();
        return {value.begin(), value.end()};
    }

    struct EnvironmentGuard
    {
        SDL_Environment* environment = SDL_GetEnvironment();
        std::optional<std::string> saved;

        EnvironmentGuard()
        {
            if (!environment) throw std::runtime_error(SDL_GetError());
            if (const char* value = SDL_GetEnvironmentVariable(environment, RootVariable))
            {
                saved = value;
            }
        }
        ~EnvironmentGuard()
        {
            if (saved)
                SDL_SetEnvironmentVariable(environment, RootVariable, saved->c_str(), true);
            else
                SDL_UnsetEnvironmentVariable(environment, RootVariable);
        }
        void set(const char* value)
        {
            const bool result = value
                ? SDL_SetEnvironmentVariable(environment, RootVariable, value, true)
                : SDL_UnsetEnvironmentVariable(environment, RootVariable);
            if (!result) throw std::runtime_error(SDL_GetError());
        }
    };

    struct Fixture
    {
        std::filesystem::path root;
        Fixture()
        {
            // CTest lance cette suite dans le build, jamais dans Source.
            root = std::filesystem::current_path() /
                ("ResourceConfiguration-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
            if (!std::filesystem::create_directory(root))
                throw std::runtime_error("cannot create resource configuration fixture");
        }
        ~Fixture()
        {
            std::error_code error;
            std::filesystem::remove_all(root, error);
            expect(!error, "configuration fixture cleaned up");
        }
    };
}

int main()
{
    using namespace monopoly;
    try
    {
        EnvironmentGuard environment;
        Fixture fixture;
        environment.set(nullptr);
        expect(udutils::generateINIFile(), "real SDL executable root configures");
        const auto* defaults = udutils::resourcePaths();
        expect(defaults && defaults->roots().size() == 1,
            "default configuration has one explicit executable root");
        if (defaults)
        {
            const char* base = SDL_GetBasePath();
            const auto baseText = std::string(base ? base : "");
            const std::u8string baseUtf8(baseText.begin(), baseText.end());
            expect(base && std::filesystem::equivalent(
                defaults->roots().front(), std::filesystem::path(baseUtf8)),
                "default root comes from actual SDL_GetBasePath");
        }

        const auto explicitRoot = fixture.root / std::filesystem::path(u8"Donn\u00e9es");
        std::filesystem::create_directories(explicitRoot / "Dat_Mon");
        {
            std::ofstream file(explicitRoot / "Dat_Mon" / "Probe.dat");
            file << "path fixture only";
            file.close();
            expect(file.good(), "configuration probe created");
        }
        environment.set(utf8(explicitRoot).c_str());
        expect(udutils::generateINIFile(), "real SDL environment accepts UTF-8 resource root");
        const auto* configured = udutils::resourcePaths();
        expect(configured && configured->roots().size() == 1 &&
            configured->roots().front() == explicitRoot,
            "explicit root replaces rather than appends executable root");
        if (configured)
        {
            auto file = configured->resolve("dat_mon/PROBE.DAT");
            expect(file && *file == explicitRoot / "Dat_Mon" / "Probe.dat",
                "real UDUtils feeds the portable resource resolver");
        }
        expect(udutils::searchPaths().size() == 3 &&
            udutils::searchPaths().front() == explicitRoot,
            "legacy path-list accessor follows configured installation");

        for (const char* invalid : {"relative-root", ""})
        {
            environment.set(invalid);
            expect(!udutils::generateINIFile() && !udutils::resourcePaths() &&
                udutils::searchPaths().empty(),
                "invalid override clears previous config and cannot fall back");
        }
        const auto absentRoot = fixture.root / "absent-installation";
        environment.set(utf8(absentRoot).c_str());
        expect(udutils::generateINIFile(), "missing explicit installation remains configurable");
        if (const auto* paths = udutils::resourcePaths())
        {
            auto file = paths->resolve("Dat_Mon/dat_main.dat");
            expect(!file && file.error().code == data::DataErrorCode::ResourceNotFound,
                "missing installation reports absent resource without fallback");
        }
        environment.set(nullptr);
        expect(udutils::generateINIFile(), "default configuration can be restored after errors");
    }
    catch (const std::exception& error)
    {
        ++failures;
        std::cerr << "[FAIL] " << error.what() << '\n';
    }
    if (failures) return 1;
    std::cout << "Real SDL/UDUtils resource configuration tests passed.\n";
    return 0;
}
