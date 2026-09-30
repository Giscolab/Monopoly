#include "StartupResources.hpp"
#include "LegacyDataArchiveBuilder.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace
{
    int failures{};
    void expect(bool condition, const char* description)
    {
        if (!condition) { ++failures; std::cerr << "[FAIL] " << description << '\n'; }
    }
    bool sameContext(monopoly::data::ResourceContext left, monopoly::data::ResourceContext right)
    { return left.board == right.board && left.language == right.language; }

    struct SelectionGuard
    {
        monopoly::data::ResourceContext context = monopoly::startup::resourceContext();
        SDL_Environment* environment = SDL_GetEnvironment();
        std::optional<std::string> root;
        SelectionGuard()
        {
            if (!environment) throw std::runtime_error(SDL_GetError());
            if (const char* value = SDL_GetEnvironmentVariable(environment, "MONOPOLY_DATA_ROOT"))
                root = value;
        }
        ~SelectionGuard()
        {
            (void)monopoly::startup::selectResourceContext(context);
            if (root) SDL_SetEnvironmentVariable(environment, "MONOPOLY_DATA_ROOT", root->c_str(), true);
            else SDL_UnsetEnvironmentVariable(environment, "MONOPOLY_DATA_ROOT");
        }
    };

    struct Fixture
    {
        std::filesystem::path root = std::filesystem::current_path() /
            ("StartupResources-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        Fixture()
        {
            using namespace monopoly::data;
            std::filesystem::create_directories(root / "Dat_Mon");
            const std::vector<ArchiveBuildItem> native{{LegacyDataType::Native, {std::byte{1}}}};
            for (const auto& bank : coreBanks(BoardEdition::Usa))
                if (!writeLegacyDataArchive(root / bank.legacyPath, native))
                    throw std::runtime_error("cannot write synthetic USA fixture bank");
            const auto* english = findLanguageBankTriplet(LanguageId::EnglishUs);
            const std::vector<ArchiveBuildItem> text{
                {LegacyDataType::IndexTable, {std::byte{42}, std::byte{0}, std::byte{0},
                    std::byte{0}, std::byte{1}, std::byte{0}}},
                {LegacyDataType::String, {std::byte{65}, std::byte{0}, std::byte{0}, std::byte{0}}}};
            if (!writeLegacyDataArchive(root / english->text.legacyPath, text) ||
                !writeLegacyDataArchive(root / english->graphics.legacyPath, native) ||
                !writeLegacyDataArchive(root / english->dialog.legacyPath, native))
                throw std::runtime_error("cannot write synthetic English fixture banks");
        }
        ~Fixture()
        {
            std::error_code error;
            std::filesystem::remove_all(root, error);
            expect(!error, "synthetic startup fixture cleans up");
        }
    };

    struct CaptureErrors
    {
        std::ostringstream text;
        std::streambuf* original = std::cerr.rdbuf(text.rdbuf());
        ~CaptureErrors() { std::cerr.rdbuf(original); }
    };
}

int main()
{
    using namespace monopoly;
    try
    {
        SelectionGuard guard;
        expect(sameContext(startup::resourceContext(), data::ResourceContext{}),
            "process resource context defaults to USA EnglishUS");
        const std::array<std::string_view, 0> empty{};
        const auto defaults = startup::parseResourceArguments(empty);
        expect(defaults && sameContext(defaults->context, data::ResourceContext{}),
            "omitted context switches preserve USA EnglishUS defaults");
        const std::array<std::string_view, 5> arguments{
            "--edition=europe", "--language=fr", "--network-host", "28799", "--check-resources"};
        const auto parsed = startup::parseResourceArguments(arguments);
        expect(parsed && parsed->context.board == data::BoardEdition::Europe &&
            parsed->context.language == data::LanguageId::French && parsed->checkOnly &&
            parsed->remaining == std::vector<std::string_view>{"--network-host", "28799"},
            "typed French European selection preserves unrelated network arguments");
        const std::array<std::string_view, 2> british{"--edition=usa", "--language=en-uk"};
        const auto uk = startup::parseResourceArguments(british);
        expect(uk && uk->context.board == data::BoardEdition::Usa &&
            uk->context.language == data::LanguageId::EnglishUk,
            "British English switch maps to the existing source language enum");
        for (const std::string_view bad : {"--edition=", "--edition=USA", "--edition=other",
                "--language=", "--language=de", "--edition", "--language"})
            expect(!startup::parseResourceArguments(std::array{bad}),
                "empty unsupported or incomplete context switches are rejected");
        expect(!startup::parseResourceArguments(std::array<std::string_view, 2>{
                "--edition=usa", "--edition=europe"}) &&
            !startup::parseResourceArguments(std::array<std::string_view, 2>{
                "--language=fr", "--language=fr"}), "duplicate context switches are rejected");
        if (!parsed) throw std::runtime_error("French resource arguments did not parse");
        expect(startup::selectResourceContext(parsed->context).has_value() &&
            sameContext(startup::resourceContext(), parsed->context),
            "process selection publishes the same typed context used for inspection and startup");
        expect(!startup::selectResourceContext({static_cast<data::BoardEdition>(-1), data::LanguageId::French}) &&
            !startup::selectResourceContext({data::BoardEdition::Europe, static_cast<data::LanguageId>(0)}) &&
            sameContext(startup::resourceContext(), parsed->context),
            "invalid programmatic context preserves the previous selection");

        Fixture fixture;
        const auto rootUtf8 = fixture.root.u8string();
        expect(startup::selectResourceRoot(std::string(rootUtf8.begin(), rootUtf8.end())).has_value(),
            "synthetic installation root is selected through the production path");
        expect(startup::selectResourceContext({}).has_value() &&
            startup::prepareResources(false) == startup::ResourceSetupResult::Ready,
            "default USA English context still qualifies the existing eight banks");
        expect(startup::selectResourceContext(parsed->context).has_value(),
            "French European context is restored for installation qualification");
        std::string diagnostics;
        startup::ResourceSetupResult result;
        {
            CaptureErrors capture;
            result = startup::prepareResources(false);
            diagnostics = capture.text.str();
        }
        const auto* french = data::findLanguageBankTriplet(data::LanguageId::French);
        expect(result == startup::ResourceSetupResult::Failed &&
            diagnostics.find(french->text.legacyPath) != std::string::npos &&
            diagnostics.find(french->graphics.legacyPath) != std::string::npos &&
            diagnostics.find(french->dialog.legacyPath) != std::string::npos &&
            diagnostics.find("Dat_Mon/dat_borde.dat") != std::string::npos,
            "French European qualification reports missing real banks without reusing English or USA data");
    }
    catch (const std::exception& error)
    {
        ++failures;
        std::cerr << "[FAIL] " << error.what() << '\n';
    }
    SDL_Quit();
    return failures ? 1 : 0;
}
