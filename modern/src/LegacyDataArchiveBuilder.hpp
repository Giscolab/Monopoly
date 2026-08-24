#pragma once

#include "LegacyDataArchive.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <vector>

namespace monopoly::data
{
    struct ArchiveBuildItem
    {
        LegacyDataType type{ LegacyDataType::Unknown };
        DataBytes payload;

        [[nodiscard]] bool emptySlot() const noexcept
        {
            return type == LegacyDataType::Unknown && payload.empty();
        }
    };


    struct ArchiveBuildOptions
    {
        std::uint16_t patchMajor{ 1 };
        std::uint16_t patchMinor{ 0 };
        int compressionLevel{ 8 };
    };


    // Reconstruction deterministe du conteneur prouve par L_Data. Cet outil
    // ne reconstitue aucun payload retail : il emballe seulement des payloads
    // fournis explicitement par le caller.
    [[nodiscard]] std::expected<DataBytes, DataError>
    buildLegacyDataArchive(
        std::span<const ArchiveBuildItem> items,
        ArchiveBuildOptions options = {});


    [[nodiscard]] std::expected<void, DataError>
    writeLegacyDataArchive(
        const std::filesystem::path& path,
        std::span<const ArchiveBuildItem> items,
        ArchiveBuildOptions options = {});
}
