#include "LegacyDataArchiveBuilder.hpp"

#include <zlib.h>

#include <algorithm>
#include <fstream>
#include <limits>
#include <utility>

namespace monopoly::data
{
    namespace
    {
        void appendU16Le(DataBytes& bytes, std::uint16_t value)
        {
            bytes.push_back(static_cast<std::byte>(value & 0xFFU));
            bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
        }


        void appendU32Le(DataBytes& bytes, std::uint32_t value)
        {
            bytes.push_back(static_cast<std::byte>(value & 0xFFU));
            bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
            bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
            bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
        }


        void writeU32Le(
            DataBytes& bytes,
            std::size_t offset,
            std::uint32_t value)
        {
            bytes[offset] = static_cast<std::byte>(value & 0xFFU);
            bytes[offset + 1] =
                static_cast<std::byte>((value >> 8U) & 0xFFU);
            bytes[offset + 2] =
                static_cast<std::byte>((value >> 16U) & 0xFFU);
            bytes[offset + 3] =
                static_cast<std::byte>((value >> 24U) & 0xFFU);
        }


        [[nodiscard]] DataError buildError(std::string detail)
        {
            return
            {
                DataErrorCode::ArchiveBuildFailed,
                {},
                std::nullopt,
                std::move(detail)
            };
        }
    }


    std::expected<DataBytes, DataError> buildLegacyDataArchive(
        std::span<const ArchiveBuildItem> items,
        ArchiveBuildOptions options)
    {
        if (items.empty())
        {
            return std::unexpected(buildError(
                "legacy DAT archives require at least one index item"));
        }

        if (items.size() > LegacyDataArchive::MaximumItemCount)
        {
            return std::unexpected(buildError(
                "archive item count exceeds the 16-bit DataTag space"));
        }

        if (options.compressionLevel < Z_NO_COMPRESSION ||
            options.compressionLevel > Z_BEST_COMPRESSION)
        {
            return std::unexpected(buildError(
                "zlib compression level must be between 0 and 9"));
        }

        struct EncodedItem
        {
            LegacyDataType type{};
            std::uint32_t offset{};
            std::uint32_t uncompressedSize{};
            DataBytes compressed;
            bool empty{};
        };

        std::vector<EncodedItem> encoded;
        encoded.reserve(items.size());

        const std::uint64_t dataStart =
            LegacyDataArchive::PhysicalHeaderSize +
            items.size() * LegacyDataArchive::PhysicalIndexRecordSize;
        std::uint64_t nextOffset = dataStart;

        for (const auto& item : items)
        {
            if (item.emptySlot())
            {
                encoded.push_back(
                    { LegacyDataType::Unknown, 0, 0, {}, true });
                continue;
            }

            if (item.type == LegacyDataType::Unknown || item.payload.empty())
            {
                return std::unexpected(buildError(
                    "present items require a non-Unknown type and payload"));
            }

            if (item.payload.size() > static_cast<std::size_t>(
                    std::numeric_limits<std::int32_t>::max()))
            {
                return std::unexpected(buildError(
                    "uncompressed item exceeds the signed Win32 size field"));
            }

            uLongf compressedCapacity = compressBound(
                static_cast<uLong>(item.payload.size()));
            DataBytes compressed(compressedCapacity);

            const int result = compress2(
                reinterpret_cast<Bytef*>(compressed.data()),
                &compressedCapacity,
                reinterpret_cast<const Bytef*>(item.payload.data()),
                static_cast<uLong>(item.payload.size()),
                options.compressionLevel);

            if (result != Z_OK)
            {
                return std::unexpected(buildError(
                    "zlib failed while compressing an archive item"));
            }

            compressed.resize(compressedCapacity);

            if (compressed.size() > static_cast<std::size_t>(
                    std::numeric_limits<std::int32_t>::max()) ||
                nextOffset > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int32_t>::max()) ||
                nextOffset + compressed.size() > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int32_t>::max()))
            {
                return std::unexpected(buildError(
                    "archive offsets exceed the signed Win32 DAT fields"));
            }

            encoded.push_back(
            {
                item.type,
                static_cast<std::uint32_t>(nextOffset),
                static_cast<std::uint32_t>(item.payload.size()),
                std::move(compressed),
                false
            });

            nextOffset += encoded.back().compressed.size();
        }

        DataBytes archive;
        archive.reserve(static_cast<std::size_t>(nextOffset));

        for (const char value : { 'A', 'r', 't', 'e', 'c', 'h', '\0', '\0' })
        {
            archive.push_back(static_cast<std::byte>(value));
        }

        appendU16Le(archive, LegacyDataArchive::SupportedVersionMajor);
        appendU16Le(archive, LegacyDataArchive::SupportedVersionMinor);
        appendU16Le(archive, options.patchMajor);
        appendU16Le(archive, options.patchMinor);
        appendU16Le(archive, 0);
        appendU16Le(archive, 0);
        appendU32Le(archive, static_cast<std::uint32_t>(items.size()));
        appendU32Le(archive, 0); // CRC rempli apres index et payloads.

        for (const auto& item : encoded)
        {
            if (item.empty)
            {
                appendU32Le(archive, 0);
                appendU32Le(archive, 0);
                appendU32Le(archive, 0);
                appendU32Le(archive, 0);
                continue;
            }

            appendU32Le(archive, item.offset);
            appendU32Le(archive, item.uncompressedSize);
            appendU32Le(
                archive,
                static_cast<std::uint32_t>(item.type));
            appendU32Le(
                archive,
                static_cast<std::uint32_t>(item.compressed.size()));
        }

        for (const auto& item : encoded)
        {
            archive.insert(
                archive.end(),
                item.compressed.begin(),
                item.compressed.end());
        }

        uLong checksum = crc32(0L, Z_NULL, 0);
        const auto checksumBytes = std::span<const std::byte>(archive).subspan(
            LegacyDataArchive::PhysicalHeaderSize);

        std::size_t offset = 0;

        while (offset < checksumBytes.size())
        {
            const auto amount = std::min<std::size_t>(
                checksumBytes.size() - offset,
                static_cast<std::size_t>(
                    std::numeric_limits<uInt>::max()));
            checksum = crc32(
                checksum,
                reinterpret_cast<const Bytef*>(
                    checksumBytes.data() + offset),
                static_cast<uInt>(amount));
            offset += amount;
        }

        writeU32Le(
            archive,
            24,
            static_cast<std::uint32_t>(checksum));
        return archive;
    }


    std::expected<void, DataError> writeLegacyDataArchive(
        const std::filesystem::path& path,
        std::span<const ArchiveBuildItem> items,
        ArchiveBuildOptions options)
    {
        auto archive = buildLegacyDataArchive(items, options);

        if (!archive)
        {
            return std::unexpected(archive.error());
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);

        if (!output)
        {
            return std::unexpected(DataError
            {
                DataErrorCode::WriteFailed,
                path,
                std::nullopt,
                "unable to create output DAT archive"
            });
        }

        output.write(
            reinterpret_cast<const char*>(archive->data()),
            static_cast<std::streamsize>(archive->size()));

        if (!output)
        {
            return std::unexpected(DataError
            {
                DataErrorCode::WriteFailed,
                path,
                std::nullopt,
                "unable to write complete output DAT archive"
            });
        }

        return {};
    }
}
