#include "LegacyDataArchiveBuilder.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace monopoly::data;
    int failures{};

    void expect(bool condition, const char* message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "[FAIL] " << message << '\n';
        }
    }

    std::string utf8(const std::filesystem::path& path)
    {
        const auto value = path.u8string();
        return {value.begin(), value.end()};
    }

    struct Fixture
    {
        std::filesystem::path root = std::filesystem::current_path() /
            ("ResourceStartupCli-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count())) /
            std::filesystem::path(u8"Donn\u00e9es du jeu \u65e5\u672c");

        Fixture()
        {
            std::filesystem::create_directories(root / "Dat_Mon");
            // Minimal test banks only. These are not retail game resources.
            const std::array files{"dat_main.dat", "dat_pat.dat", "dat_bord.dat",
                "dat_brd2.dat", "dat_3d.dat", "dat_ln01.dat", "dat_lm01.dat", "dat_lk01.dat"};
            for (std::size_t index = 0; index < files.size(); ++index)
            {
                const std::vector<ArchiveBuildItem> items = index == 5
                    ? std::vector<ArchiveBuildItem>{
                        {LegacyDataType::IndexTable, {std::byte{42}, std::byte{0},
                            std::byte{0}, std::byte{0}, std::byte{1}, std::byte{0}}},
                        {LegacyDataType::String, {std::byte{65}, std::byte{0},
                            std::byte{0}, std::byte{0}}}}
                    : std::vector<ArchiveBuildItem>{
                        {LegacyDataType::Native, {std::byte{1}}}};
                const auto written = writeLegacyDataArchive(root / "Dat_Mon" / files[index], items);
                if (!written) throw std::runtime_error(written.error().detail);
            }
        }

        ~Fixture()
        {
            std::error_code error;
            std::filesystem::remove_all(root.parent_path(), error);
            expect(!error, "CLI fixture cleaned up");
        }
    };

    struct Result { int status; std::string output; };

    Result run(const std::vector<std::string>& arguments)
    {
        std::vector<const char*> pointers;
        for (const auto& argument : arguments) pointers.push_back(argument.c_str());
        pointers.push_back(nullptr);
        auto* process = SDL_CreateProcess(pointers.data(), true);
        if (!process) throw std::runtime_error(SDL_GetError());
        std::size_t length = 0;
        int status = -1;
        void* bytes = SDL_ReadProcess(process, &length, &status);
        std::string output;
        if (bytes) output.assign(static_cast<const char*>(bytes), length);
        SDL_free(bytes);
        const bool waited = SDL_WaitProcess(process, true, &status);
        SDL_DestroyProcess(process);
        if (!waited) throw std::runtime_error(SDL_GetError());
        return {status, std::move(output)};
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("expected the application executable path");
        Fixture fixture;
        const std::string executable(argv[1]);
        const auto root = utf8(fixture.root);
        auto complete = run({executable, "--data-root", root, "--check-resources"});
        expect(complete.status == 0 && complete.output.find(root) != std::string::npos,
            "real application opens eight banks under a Unicode path with spaces");
        auto network = run({executable, "--network-connect", "256.256.256.256:28799",
            "--check-resources", "--data-root", root});
        expect(network.status == 0,
            "resource-only mode preserves network arguments but never opens the connection");
        auto missing = run({executable, "--data-root", utf8(fixture.root / "absent"),
            "--check-resources"});
        expect(missing.status == 1, "missing installation returns failure without a dialog");
        auto relative = run({executable, "--data-root", "relative-folder", "--check-resources"});
        expect(relative.status == 1, "relative installation is rejected at the application boundary");
        auto invalid = run({executable, "--check-resources", "--unknown"});
        expect(invalid.status == 1, "unknown application options are rejected");
    }
    catch (const std::exception& error)
    {
        ++failures;
        std::cerr << "[FAIL] " << error.what() << '\n';
    }
    SDL_Quit();
    return failures ? 1 : 0;
}
