#include "ResourceRuntime.hpp"
#include "LegacyDataArchiveBuilder.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
    using namespace monopoly::data;
    int failures{};

    void expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "[FAIL] " << message << '\n';
        }
    }

    struct Fixture
    {
        std::filesystem::path root;
        bool valid{};

        Fixture()
        {
            const auto stamp = std::chrono::steady_clock::now()
                .time_since_epoch().count();
            std::error_code error;
            auto parent = std::filesystem::temp_directory_path(error);
            if (error)
            {
                parent = std::filesystem::current_path();
            }
            root = parent / ("MonopolyResourceRuntime-" + std::to_string(stamp));
            valid = std::filesystem::create_directory(root, error) && !error;
            if (!valid)
            {
                root = std::filesystem::current_path() /
                    ("MonopolyResourceRuntime-" + std::to_string(stamp));
                error.clear();
                valid = std::filesystem::create_directory(root, error) && !error;
            }
            expect(valid, "fixture directory created");
        }

        ~Fixture()
        {
            if (valid)
            {
                std::error_code ignored;
                std::filesystem::remove_all(root, ignored);
            }
        }
    };

    // Format prouve par L_Data : index (u32 message, u16 tag), UTF-16LE NUL.
    // Ce texte marque uniquement une fixture, jamais une chaine du jeu.
    std::vector<ArchiveBuildItem> textItems(char16_t marker)
    {
        return {
            { LegacyDataType::IndexTable, {
                std::byte{42}, std::byte{0}, std::byte{0}, std::byte{0},
                std::byte{1}, std::byte{0} } },
            { LegacyDataType::String, {
                static_cast<std::byte>(marker & 255),
                static_cast<std::byte>(marker >> 8), std::byte{0}, std::byte{0} } }
        };
    }

    bool writeBank(const std::filesystem::path& path, bool text = false,
        char16_t marker = u'A', bool invalidIndex = false)
    {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
        {
            expect(false, "fixture bank parent created");
            return false;
        }
        auto items = text ? textItems(marker) : std::vector<ArchiveBuildItem>{
            { LegacyDataType::Native, {std::byte{0x12}, std::byte{0x34}} },
            { LegacyDataType::Native, {std::byte{0x56}, std::byte{0x78}} } };
        if (invalidIndex)
        {
            items[0].payload.pop_back();
        }
        auto written = writeLegacyDataArchive(path, items);
        expect(written.has_value(), "synthetic archive written");
        return written.has_value();
    }

    const std::array<std::string_view, 8> usaFiles{
        "dat_main.dat", "dat_pat.dat", "dat_bord.dat", "dat_brd2.dat",
        "dat_3d.dat", "dat_ln01.dat", "dat_lm01.dat", "dat_lk01.dat" };

    bool writeSet(const std::filesystem::path& root,
        const std::array<std::string_view, 8>& files = usaFiles,
        char16_t marker = u'A', bool invalidIndex = false)
    {
        for (std::size_t i = 0; i < files.size(); ++i)
        {
            if (!writeBank(root / "Dat_Mon" / files[i], i == 5,
                marker, invalidIndex && i == 5))
            {
                return false;
            }
        }
        return true;
    }

    ResourcePaths pathsFor(const std::filesystem::path& root)
    {
        auto paths = ResourcePaths::create(std::array{root});
        if (!paths)
        {
            throw std::runtime_error("fixture root rejected: " + paths.error().detail);
        }
        return *paths;
    }

    ArchiveOpenOptions verified()
    {
        ArchiveOpenOptions options;
        options.checksumPolicy = ChecksumPolicy::Verify;
        return options;
    }

    void testStartupAndLifetime()
    {
        Fixture fixture;
        if (!fixture.valid || !writeSet(fixture.root)) return;
        ResourceRuntime runtime;
        expect(!runtime.snapshot(), "runtime starts empty");
        auto initialized = runtime.initialize(pathsFor(fixture.root), {}, verified());
        expect(initialized.has_value(), "complete eight-bank set initializes");
        auto snapshot = runtime.snapshot();
        if (!snapshot) return;
        expect(snapshot->banks().mountedCount() == 8, "exactly eight source groups mounted");
        auto language = snapshot->language();
        expect(language && language->language == LanguageId::EnglishUs,
            "default USA language is English US");
        if (!language) return;
        expect(snapshot->banks().archive(9).value() == language->textArchive &&
            snapshot->banks().archive(5).value() == language->mediaArchive &&
            snapshot->banks().archive(10).value() == language->dialogArchive,
            "DATA and LANG hold identical archives, not duplicate opens");
        const auto text = language->catalog->message(42);
        expect(text && **text == u"A", "real catalog decodes text from shared bank");
        const auto data = snapshot->banks().load(packDataId(2, 0));
        expect(data && **data == DataBytes{std::byte{0x12}, std::byte{0x34}},
            "DATA lookup routes the packed source ID");

        runtime.shutdown();
        runtime.shutdown();
        expect(!runtime.snapshot(), "shutdown is idempotent and unpublishes active set");
        auto lateLoad = snapshot->banks().load(packDataId(2, 1));
        expect(lateLoad && **lateLoad == DataBytes{std::byte{0x56}, std::byte{0x78}},
            "held snapshot can load UNCACHED bytes after service shutdown");
        snapshot.reset();
        const auto media = language->mediaArchive->load(1);
        expect(media.has_value(), "language snapshot alone keeps media archive open");
        language.reset();
        expect(text && **text == u"A" && data && (**data)[0] == std::byte{0x12},
            "owned text and bytes survive all snapshots");
        expect(runtime.initialize(pathsFor(fixture.root), {}, verified()).has_value(),
            "runtime restarts after shutdown");
    }

    void testFailuresAreTransactional()
    {
        Fixture fixture;
        if (!fixture.valid || !writeSet(fixture.root / "good")) return;
        ResourceRuntime runtime;
        expect(runtime.initialize(pathsFor(fixture.root / "good"), {}, verified()).has_value(),
            "known valid snapshot established");
        const auto original = runtime.snapshot();
        if (!original) return;

        // Chacune des huit positions peut echouer apres des ouvertures reelles.
        for (std::size_t missing = 0; missing < usaFiles.size(); ++missing)
        {
            const auto root = fixture.root / ("missing-" + std::to_string(missing));
            for (std::size_t i = 0; i < usaFiles.size(); ++i)
            {
                if (i != missing) writeBank(root / "Dat_Mon" / usaFiles[i], i == 5);
            }
            auto result = runtime.initialize(pathsFor(root), {}, verified());
            expect(!result && result.error().code == DataErrorCode::ResourceNotFound &&
                result.error().detail.find(usaFiles[missing]) != std::string::npos,
                "missing bank produces a precise resource error");
            expect(runtime.snapshot() == original,
                "missing bank at any position leaves active snapshot unchanged");
        }
        for (std::size_t corrupt = 0; corrupt < usaFiles.size(); ++corrupt)
        {
            const auto root = fixture.root / ("corrupt-" + std::to_string(corrupt));
            if (!writeSet(root)) return;
            // Tronquer le fichier cree par cette fixture; aucune banque retail.
            std::filesystem::resize_file(root / "Dat_Mon" / usaFiles[corrupt], 7);
            const auto result = runtime.initialize(pathsFor(root), {}, verified());
            expect(!result && result.error().code == DataErrorCode::HeaderTruncated,
                "truncated bank at any position propagates DAT error");
            expect(runtime.snapshot() == original,
                "corrupt bank cannot publish a partial registry");
        }
        const auto invalidRoot = fixture.root / "invalid-index";
        if (!writeSet(invalidRoot, usaFiles, u'A', true)) return;
        auto invalidIndex = runtime.initialize(pathsFor(invalidRoot), {}, verified());
        expect(!invalidIndex && invalidIndex.error().code == DataErrorCode::InvalidIndexTable &&
            runtime.snapshot() == original, "invalid language index rolls back eight mounted banks");
        auto badLanguage = runtime.initialize(pathsFor(fixture.root),
            {BoardEdition::Usa, static_cast<LanguageId>(0)});
        expect(!badLanguage && badLanguage.error().code == DataErrorCode::InvalidLanguage &&
            runtime.snapshot() == original, "invalid language rejected before file access");
        auto badBoard = runtime.initialize(pathsFor(fixture.root),
            {static_cast<BoardEdition>(99), LanguageId::EnglishUs});
        expect(!badBoard && badBoard.error().code == DataErrorCode::InvalidBoardEdition &&
            runtime.snapshot() == original, "invalid board does not default silently to USA");

        ResourceRuntime empty;
        auto absent = empty.initialize(pathsFor(fixture.root / "absent"));
        expect(!absent && absent.error().detail.find("dat_main.dat") != std::string::npos &&
            !empty.snapshot(), "empty installation fails on first core bank without fallback");
    }

    void testEditionAndReplacement()
    {
        Fixture fixture;
        if (!fixture.valid) return;
        const std::array<std::string_view, 8> europeFrench{
            "dat_main.dat", "dat_pat.dat", "dat_borde.dat", "dat_brd2.dat",
            "dat_3d.dat", "dat_ln03.dat", "dat_lm03.dat", "dat_lk03.dat" };
        if (!writeSet(fixture.root / "usa") ||
            !writeSet(fixture.root / "europe", europeFrench, u'F')) return;
        ResourceRuntime runtime;
        expect(runtime.initialize(pathsFor(fixture.root / "usa")).has_value(), "USA initializes");
        auto old = runtime.snapshot();
        const ResourceContext french{BoardEdition::Europe, LanguageId::French};
        auto changed = runtime.initialize(pathsFor(fixture.root / "europe"), french, verified());
        auto current = runtime.snapshot();
        expect(changed && current && current != old, "complete Europe/French set replaces USA atomically");
        if (!changed || !current || !old) return;
        expect(current->context().board == BoardEdition::Europe &&
            current->banks().archive(6).value()->path().filename() == "dat_borde.dat",
            "Europe selects the source-defined European board");
        auto text = current->language()->catalog->message(42);
        expect(text && **text == u"F", "French selection reads suffix 03 text");
        auto lateOld = old->banks().load(packDataId(8, 1));
        expect(lateOld.has_value(), "replacement preserves uncached old snapshot loads");
        auto wrongEdition = runtime.initialize(pathsFor(fixture.root / "europe"));
        expect(!wrongEdition && wrongEdition.error().detail.find("dat_bord.dat") != std::string::npos &&
            runtime.snapshot() == current, "USA cannot fall back to European board");
    }
}

int main()
{
    try
    {
        testStartupAndLifetime();
        testFailuresAreTransactional();
        testEditionAndReplacement();
    }
    catch (const std::exception& error)
    {
        expect(false, error.what());
    }
    if (failures != 0) return 1;
    std::cout << "Resource runtime contract tests passed (synthetic DAT fixtures).\n";
    return 0;
}
