#include "LegacyManifest.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace
{
    int failures = 0;


    void expect(bool condition, std::string_view description)
    {
        if (condition)
        {
            std::cout << "[PASS] " << description << '\n';
            return;
        }

        ++failures;
        std::cerr << "[FAIL] " << description << '\n';
    }


    std::string syntheticHeader(
        std::string_view definitions,
        std::size_t count = 3)
    {
        std::ostringstream output;
        output
            << "/*\n"
            << "DMAKE99: Data file builder ver 2.03 for Win95.\n"
            << "There were " << count
            << " unique items in the DF file.\n"
            << "*/\n"
            << "#define DEF_blank -1\n"
            << definitions;
        return output.str();
    }


    void testSyntheticParser()
    {
        using namespace monopoly::data;

        auto text = syntheticHeader(
            "#define BMP_first 0x0000\n"
            "#define TAB_second 0x0001\n"
            "#define WAV_third 0x0002\n");
        std::istringstream input(text);
        auto parsed = parseDmakeManifest(input, "synthetic");

        expect(parsed.has_value(), "synthetic DMake manifest parses");

        if (parsed)
        {
            expect(parsed->declaredItemCount == 3, "declared count parsed");
            expect(parsed->entries.size() == 3, "three definitions parsed");
            expect(
                parsed->entries[0].type == LegacyDataType::Bitmap &&
                    parsed->entries[0].tag == 0,
                "BMP prefix maps to DataBMP and tag zero");
            expect(
                parsed->entries[1].type == LegacyDataType::Uap,
                "TAB prefix maps to DataUAP");
            expect(
                parsed->entries[2].type == LegacyDataType::Wave,
                "WAV prefix maps to Wave");
        }
    }


    void expectFailure(
        std::string text,
        monopoly::data::ManifestErrorCode expected,
        std::string_view description)
    {
        std::istringstream input(std::move(text));
        auto parsed = monopoly::data::parseDmakeManifest(input, "invalid");
        expect(
            !parsed && parsed.error().code == expected,
            description);
    }


    void testDeterministicFailures()
    {
        using namespace monopoly::data;

        expectFailure(
            "There were 0 unique items in the DF file.\n",
            ManifestErrorCode::MissingGeneratorSignature,
            "missing generator signature is deterministic");

        expectFailure(
            "DMAKE99: Data file builder ver 2.03 for Win95.\n",
            ManifestErrorCode::MissingDeclaredCount,
            "missing declared count is deterministic");

        expectFailure(
            syntheticHeader("#define XYZ_unknown 0x0000\n", 1),
            ManifestErrorCode::UnknownItemPrefix,
            "unknown item prefix is rejected");

        expectFailure(
            syntheticHeader(
                "#define BMP_first 0x0000\n"
                "#define BMP_gap 0x0002\n",
                2),
            ManifestErrorCode::NonContiguousTag,
            "non-contiguous tags are rejected");

        expectFailure(
            syntheticHeader(
                "#define BMP_first 0x0000\n"
                "#define BMP_first 0x0001\n",
                2),
            ManifestErrorCode::DuplicateSymbol,
            "duplicate symbols are rejected");

        expectFailure(
            syntheticHeader("#define BMP_large 0x10000\n", 1),
            ManifestErrorCode::TagOutOfRange,
            "tags larger than 16 bits are rejected");

        expectFailure(
            syntheticHeader("#define BMP_only 0x0000\n", 2),
            ManifestErrorCode::CountMismatch,
            "declared/parsed count mismatch is rejected");
    }


    void testRetailSourceManifests()
    {
#ifdef MONOPOLY_LEGACY_SOURCE_DIR
        using namespace monopoly::data;

        struct ExpectedHeader
        {
            std::string_view name;
            std::size_t count;
        };

        constexpr std::array Headers
        {
            ExpectedHeader{ "dat_3d.h", 1'647 },
            ExpectedHeader{ "dat_bord.h", 7'411 },
            ExpectedHeader{ "DAT_BORDE.h", 8'839 },
            ExpectedHeader{ "dat_brd2.h", 6'552 },
            ExpectedHeader{ "dat_lk01.h", 2'859 },
            ExpectedHeader{ "dat_lk02.h", 2'928 },
            ExpectedHeader{ "dat_lm01.h", 5'885 },
            ExpectedHeader{ "dat_lm02.h", 6'623 },
            ExpectedHeader{ "dat_main.h", 2'140 },
            ExpectedHeader{ "dat_pat.h", 1'539 }
        };

        const auto root = std::filesystem::path(MONOPOLY_LEGACY_SOURCE_DIR) /
            "monopoly" / "Dat_Mon";
        std::size_t total = 0;
        std::unordered_map<LegacyDataType, std::size_t> typeCounts;

        for (const auto& expected : Headers)
        {
            auto manifest = readDmakeManifest(root / expected.name);
            expect(
                manifest.has_value(),
                std::string("parse source manifest ") +
                    std::string(expected.name));

            if (!manifest)
            {
                continue;
            }

            expect(
                manifest->entries.size() == expected.count,
                std::string(expected.name) + " exact item count");
            total += manifest->entries.size();

            for (const auto& entry : manifest->entries)
            {
                ++typeCounts[entry.type];
            }
        }

        expect(total == 46'423, "ten DMake manifests contain 46,423 items");
        expect(
            typeCounts[LegacyDataType::Bitmap] == 950,
            "DMake manifests contain 950 BMP items");
        expect(
            typeCounts[LegacyDataType::Chunky] == 3'422,
            "DMake manifests contain 3,422 CNK items");
        expect(
            typeCounts[LegacyDataType::Hmd] == 245,
            "DMake manifests contain 245 HMD items");
        expect(
            typeCounts[LegacyDataType::Uap] == 35'460,
            "DMake manifests contain 35,460 TAB/UAP items");
        expect(
            typeCounts[LegacyDataType::Wave] == 6'346,
            "DMake manifests contain 6,346 WAV items");
#else
        std::cout
            << "[SKIP] optional retail DMake manifest audit "
               "(legacy Source tree not available)\n";
#endif
    }


    void testTsvWriter()
    {
        using namespace monopoly::data;

        auto text = syntheticHeader(
            "#define BMP_first 0x0000\n"
            "#define CNK_second 0x0001\n",
            2);
        std::istringstream input(text);
        auto parsed = parseDmakeManifest(input, "bank");
        const auto outputPath =
            std::filesystem::current_path() / "manifest-test-output.tsv";

        if (!parsed)
        {
            expect(false, "TSV fixture parses before write");
            return;
        }

        const std::array manifests{ *parsed };
        const auto written = writeManifestTsv(outputPath, manifests);
        expect(written.has_value(), "TSV manifest is written");

        std::string outputText;

        {
            std::ifstream output(outputPath);
            std::ostringstream contents;
            contents << output.rdbuf();
            outputText = contents.str();
        }

        expect(
            outputText ==
                "bank\ttag\ttype\tsymbol\n"
                "bank\t0\tBMP\tBMP_first\n"
                "bank\t1\tChunky\tCNK_second\n",
            "TSV output is deterministic");

        std::error_code removeError;
        const bool removed = std::filesystem::remove(
            outputPath,
            removeError);
        expect(
            removed && !removeError,
            "TSV test output is removed after its reader closes");
    }
}


int main()
{
    testSyntheticParser();
    testDeterministicFailures();
    testRetailSourceManifests();
    testTsvWriter();

    if (failures != 0)
    {
        std::cerr << failures << " manifest test(s) failed\n";
        return 1;
    }

    std::cout << "All legacy manifest tests passed\n";
    return 0;
}
