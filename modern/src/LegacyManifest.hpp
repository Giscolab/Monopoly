#pragma once

#include "LegacyDataArchive.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <istream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace monopoly::data
{
    enum class ManifestErrorCode
    {
        None,
        FileOpenFailed,
        MissingGeneratorSignature,
        MissingDeclaredCount,
        MalformedDefinition,
        UnknownItemPrefix,
        TagOutOfRange,
        DuplicateSymbol,
        DuplicateTag,
        NonContiguousTag,
        CountMismatch,
        OutputOpenFailed,
        OutputWriteFailed
    };


    struct ManifestError
    {
        ManifestErrorCode code{ ManifestErrorCode::None };
        std::filesystem::path path;
        std::size_t line{};
        std::string detail;
    };


    struct LegacyManifestEntry
    {
        DataTag tag{};
        LegacyDataType type{ LegacyDataType::Unknown };
        std::string symbol;
    };


    struct LegacyBankManifest
    {
        std::string bankName;
        std::uint32_t declaredItemCount{};
        std::vector<LegacyManifestEntry> entries;
    };


    [[nodiscard]] std::string_view manifestErrorCodeName(
        ManifestErrorCode code) noexcept;

    // Parse uniquement le manifeste de symboles produit par DMAKE99. Il ne
    // reconstruit ni payload, ni offset, ni taille, ni CRC d'une banque DAT.
    [[nodiscard]] std::expected<LegacyBankManifest, ManifestError>
    parseDmakeManifest(
        std::istream& input,
        std::string bankName,
        std::filesystem::path diagnosticPath = {});

    [[nodiscard]] std::expected<LegacyBankManifest, ManifestError>
    readDmakeManifest(const std::filesystem::path& path);

    // Format stable et volontairement simple pour les outils build-time :
    // bank<TAB>tag_decimal<TAB>type<TAB>symbol.
    [[nodiscard]] std::expected<void, ManifestError>
    writeManifestTsv(
        const std::filesystem::path& outputPath,
        std::span<const LegacyBankManifest> manifests);
}
