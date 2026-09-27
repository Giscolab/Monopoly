#pragma once

#include "LegacyDataArchiveBuilder.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace monopoly::data::assembly
{
    struct Request
    {
        std::filesystem::path sourceRoot; // Source/monopoly, read-only
        std::filesystem::path outputRoot; // Isolated reconstruction workspace
        std::vector<std::filesystem::path> extraRoots;
        std::optional<std::filesystem::path> mappings;
        bool requireComplete{};
    };

    struct BankResult
    {
        std::string name;
        std::size_t expected{};
        std::size_t available{};
        std::size_t invalid{};
        std::filesystem::path archive;
        [[nodiscard]] bool complete() const noexcept
        { return expected != 0 && available == expected && invalid == 0; }
    };

    struct Result
    {
        std::filesystem::path directory;
        std::vector<BankResult> banks;
        [[nodiscard]] bool complete() const noexcept;
    };

    // Assembly never installs incomplete banks into the application's Dat_Mon.
    // Failure to resolve a payload is recorded, never substituted with an image,
    // fabricated animation, silent WAV or a compacted (renumbered) data index.
    [[nodiscard]] Result run(const Request& request);

    struct EnglishText
    {
        std::map<std::uint32_t, std::string> requiredNames;
        std::map<std::uint32_t, std::string> text;
        std::vector<std::string> unmappedSymbols;
    };

    // The supplied English.atr uses ASCII, symbolic IDs and braced messages.
    // Only exact symbols in the active dat_lang.h are mapped. English.A belongs
    // to a different compiled format and its numeric offsets are not guessed.
    [[nodiscard]] EnglishText readEnglishText(
        const std::filesystem::path& definitions,
        const std::filesystem::path& atr);
    [[nodiscard]] std::vector<ArchiveBuildItem> buildEnglishItems(
        const EnglishText& input);
}
