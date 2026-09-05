#include "ResourcePaths.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
    using monopoly::data::DataErrorCode;
    using monopoly::data::ResourcePaths;

    int failures = 0;


    void expect(bool condition, std::string_view description)
    {
        if (condition)
        {
            std::cout << "[PASS] " << description << '\n';
        }
        else
        {
            ++failures;
            std::cerr << "[FAIL] " << description << '\n';
        }
    }


    template<typename T>
    void expectError(
        const std::expected<T, monopoly::data::DataError>& result,
        DataErrorCode code,
        std::string_view description)
    {
        expect(!result && result.error().code == code, description);
        if (!result && result.error().code != code)
        {
            std::cerr << "       " << result.error().detail << '\n';
        }
    }


    ResourcePaths makePaths(std::span<const std::filesystem::path> roots)
    {
        auto result = ResourcePaths::create(roots);
        if (!result)
        {
            throw std::runtime_error(result.error().detail);
        }
        return std::move(*result);
    }


    class TemporaryDirectory final
    {
    public:
        TemporaryDirectory()
        {
            const auto token = std::chrono::high_resolution_clock::now()
                .time_since_epoch().count();
            const auto name = "MonopolyResourcePathsTests-" + std::to_string(token);
            const auto tryCreate = [&] (const std::filesystem::path& root)
            {
                std::error_code error;
                const auto candidate = root / name;
                if (candidate.is_absolute() &&
                    std::filesystem::create_directory(candidate, error))
                {
                    path_ = candidate;
                    return true;
                }
                return false;
            };

            std::error_code error;
            const auto temporary = std::filesystem::temp_directory_path(error);
            if (!error && tryCreate(temporary))
            {
                return;
            }

            const auto workingDirectory = std::filesystem::current_path(error);
            if (!error && tryCreate(workingDirectory))
            {
                return;
            }
            throw std::runtime_error("cannot create isolated resource path fixtures");
        }

        TemporaryDirectory(const TemporaryDirectory&) = delete;
        TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }

        [[nodiscard]] const std::filesystem::path& path() const noexcept
        {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };


    void writeFixture(const std::filesystem::path& path)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << "resource path fixture";
        file.close();
        if (!file)
        {
            throw std::runtime_error("cannot write resource path fixture");
        }
    }


    void testResolution(const std::filesystem::path& base)
    {
        const auto first = base / "first";
        const auto second = base / "second";
        const auto missing = base / "not-installed";
        const auto firstBank = first / "Dat_Mon" / "DAT_MAIN.DAT";
        const auto secondBank = second / "dat_mon" / "dat_main.dat";
        writeFixture(firstBank);
        writeFixture(secondBank);
        // Keep the actual parent spelling on a case-sensitive filesystem.
        writeFixture(second / "dat_mon" / "dat_pat.dat");
        writeFixture(first / "orphan.dat");

        const std::array roots{ missing, first / ".", first, second };
        const auto paths = makePaths(roots);
        expect(paths.roots().size() == 3 && paths.roots()[0] == missing &&
            paths.roots()[1] == first && paths.roots()[2] == second,
            "absolute roots are lexically normalized, deduplicated and ordered");

        const auto bank = paths.resolve("dAt_mOn\\dat_main.dat");
        expect(bank && *bank == firstBank,
            "legacy backslash and ASCII case resolve the first matching root");

        const auto next = paths.resolve("Dat_Mon/Dat_Pat.dat");
        expect(next && *next == second / "dat_mon" / "dat_pat.dat",
            "a file absent from earlier roots resolves in a later root");

        const std::array reversedRoots{ second, first };
        const auto reversed = makePaths(reversedRoots);
        const auto reversedBank = reversed.resolve("DAT_MON/DAT_MAIN.DAT");
        expect(reversedBank && *reversedBank == secondBank,
            "explicit root order determines resource precedence");

        expectError(paths.resolve("Dat_Mon/orphan.dat"),
            DataErrorCode::ResourceNotFound,
            "an existing basename never replaces a missing relative path");
        expectError(paths.resolve("Dat_Mon/absent.dat"),
            DataErrorCode::ResourceNotFound, "missing resource reports not found");
        expectError(paths.resolve("Dat_Mon"), DataErrorCode::ResourceNotFound,
            "a directory cannot be returned as a resource file");
        expectError(paths.resolve("orphan.dat/."), DataErrorCode::ResourcePathInvalid,
            "a terminal dot cannot turn a file into a directory traversal");

        const auto unicodeFile = first / std::filesystem::path(u8"Donn\u00e9es") /
            std::filesystem::path(u8"Fichier_\u00e9.dat");
        writeFixture(unicodeFile);
        const auto unicode = paths.resolve("Donn\xc3\xa9" "es/FICHIER_\xc3\xa9.dat");
        expect(unicode && *unicode == unicodeFile,
            "UTF-8 components are preserved while ASCII letters ignore case");

        std::filesystem::create_directories(first / "dir-only.dat");
        writeFixture(second / "dir-only.dat");
        const auto regularOnly = paths.resolve("dir-only.dat");
        expect(regularOnly && *regularOnly == second / "dir-only.dat",
            "only a regular file satisfies a resource request");
    }


    void testInvalidPaths(const std::filesystem::path& base)
    {
        const std::array roots{ base };
        const auto paths = makePaths(roots);
        const std::array<std::string_view, 11> invalid{
            "", "/Dat_Mon/dat_main.dat", "\\Dat_Mon\\dat_main.dat",
            "C:\\Dat_Mon\\dat_main.dat", "C:dat_main.dat",
            "Dat_Mon/../dat_main.dat", "../dat_main.dat",
            "Dat_Mon\\..\\dat_main.dat", ".", "Dat_Mon/", "file:stream"
        };
        for (const auto input : invalid)
        {
            expectError(paths.resolve(input), DataErrorCode::ResourcePathInvalid,
                "rooted, drive, parent traversal or non-file input is rejected");
        }
        const std::string nulInput("Dat_Mon\0/dat_main.dat", 21);
        expectError(paths.resolve(nulInput), DataErrorCode::ResourcePathInvalid,
            "embedded NUL is rejected before filesystem access");

        expectError(ResourcePaths::create({}), DataErrorCode::ResourcePathInvalid,
            "empty roots cannot implicitly search the working directory");
        const std::array relativeRoots{ std::filesystem::path("Dat_Mon") };
        expectError(ResourcePaths::create(relativeRoots),
            DataErrorCode::ResourcePathInvalid, "relative roots are rejected");
        const std::array emptyRoot{ std::filesystem::path{} };
        expectError(ResourcePaths::create(emptyRoot),
            DataErrorCode::ResourcePathInvalid, "empty root is rejected");

        writeFixture(base / "root-is-file");
        const std::array fileRoot{ base / "root-is-file" };
        expectError(ResourcePaths::create(fileRoot),
            DataErrorCode::ResourcePathInvalid, "existing file is not a root directory");

        auto nulRoot = base.native();
        nulRoot.push_back(std::filesystem::path::value_type{});
        nulRoot += std::filesystem::path("suffix").native();
        const std::array nulRoots{ std::filesystem::path(nulRoot) };
        expectError(ResourcePaths::create(nulRoots),
            DataErrorCode::ResourcePathInvalid, "embedded NUL root is rejected");
    }


    void testAmbiguity(const std::filesystem::path& base)
    {
        const auto first = base / "collision";
        const auto second = base / "fallback";
        const auto lower = first / "item.dat";
        const auto upper = first / "ITEM.DAT";
        writeFixture(lower);
        writeFixture(upper);
        writeFixture(second / "item.dat");

        std::error_code error;
        const bool sameFile = std::filesystem::equivalent(lower, upper, error);
        if (error)
        {
            throw std::runtime_error("cannot inspect case sensitivity of fixture filesystem");
        }
        if (sameFile)
        {
            std::cout << "[SKIP] case-collision tests require a case-sensitive filesystem\n";
            return;
        }

        const std::array roots{ first, second };
        const auto paths = makePaths(roots);
        expectError(paths.resolve("item.dat"), DataErrorCode::ResourcePathAmbiguous,
            "case collision rejects even an exact match and prevents fallback");

        writeFixture(first / "Folder" / "child.dat");
        writeFixture(first / "folder" / "different.dat");
        writeFixture(second / "folder" / "child.dat");
        expectError(paths.resolve("Folder/child.dat"),
            DataErrorCode::ResourcePathAmbiguous,
            "an ambiguous directory component cannot select an arbitrary subtree");
    }


    void testFilesystemFailure(const std::filesystem::path& base)
    {
        const auto first = base / "io-failure";
        const auto second = base / "io-fallback";
        std::filesystem::create_directories(first);
        writeFixture(second / "loop" / "child.dat");
        std::error_code error;
        std::filesystem::create_directory_symlink("loop", first / "loop", error);
        if (error)
        {
            std::cout << "[SKIP] symlink-loop error test requires symlink creation rights\n";
            return;
        }

        const std::array roots{ first, second };
        const auto paths = makePaths(roots);
        expectError(paths.resolve("loop/child.dat"),
            DataErrorCode::ResourcePathFailed,
            "filesystem error in an earlier root is surfaced without fallback");
    }
}


int main()
{
    try
    {
        const TemporaryDirectory fixtures;
        testResolution(fixtures.path());
        testInvalidPaths(fixtures.path());
        testAmbiguity(fixtures.path());
        testFilesystemFailure(fixtures.path());
    }
    catch (const std::exception& error)
    {
        ++failures;
        std::cerr << "[FAIL] fixture execution: " << error.what() << '\n';
    }

    std::cout << "Resource path test failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
