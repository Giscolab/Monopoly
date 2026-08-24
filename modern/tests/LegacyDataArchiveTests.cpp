#include "LegacyDataArchive.hpp"
#include "LegacyDataArchiveBuilder.hpp"
#include "LegacyChunk.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using monopoly::data::ArchiveBuildItem;
    using monopoly::data::ArchiveBuildOptions;
    using monopoly::data::ArchiveOpenOptions;
    using monopoly::data::ChecksumPolicy;
    using monopoly::data::DataBankRegistry;
    using monopoly::data::DataBytes;
    using monopoly::data::DataError;
    using monopoly::data::DataErrorCode;
    using monopoly::data::DataIndexTable;
    using monopoly::data::DataTag;
    using monopoly::data::LegacyDataArchive;
    using monopoly::data::LegacyDataType;
    using monopoly::data::SharedDataBytes;


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


    template<typename Value>
    void expectError(
        const std::expected<Value, DataError>& result,
        DataErrorCode expectedCode,
        std::string_view description)
    {
        expect(
            !result && result.error().code == expectedCode,
            description);

        if (!result && result.error().code != expectedCode)
        {
            std::cerr
                << "       expected error "
                << monopoly::data::dataErrorCodeName(expectedCode)
                << ", got "
                << monopoly::data::dataErrorCodeName(result.error().code)
                << ": "
                << result.error().detail
                << '\n';
        }
    }


    template<typename Value>
    Value requireValue(
        std::expected<Value, DataError> result,
        std::string_view operation)
    {
        if (!result)
        {
            throw std::runtime_error(
                std::string(operation) + ": " + result.error().detail);
        }

        return std::move(*result);
    }


    void requireSuccess(
        std::expected<void, DataError> result,
        std::string_view operation)
    {
        if (!result)
        {
            throw std::runtime_error(
                std::string(operation) + ": " + result.error().detail);
        }
    }


    class TemporaryDirectory final
    {
    public:
        TemporaryDirectory()
        {
            const auto token = std::chrono::high_resolution_clock::now()
                .time_since_epoch()
                .count();
            const auto directoryName =
                "MonopolyLegacyDataArchiveTests-" +
                std::to_string(token);
            std::error_code lastError;

            const auto tryCreate = [&] (const std::filesystem::path& root)
            {
                path_ = root / directoryName;
                lastError.clear();
                return std::filesystem::create_directory(path_, lastError) &&
                    !lastError;
            };

            std::error_code tempPathError;
            const auto systemTemporary =
                std::filesystem::temp_directory_path(tempPathError);

            if (!tempPathError && tryCreate(systemTemporary))
            {
                return;
            }

            // Certains runners confines exposent un TEMP systeme lisible mais
            // non inscriptible. CTest lance l'executable depuis son repertoire
            // de build, qui constitue alors un repli isole et inscriptible.
            std::error_code currentPathError;
            const auto workingDirectory =
                std::filesystem::current_path(currentPathError);

            if (!currentPathError && tryCreate(workingDirectory))
            {
                return;
            }

            if (lastError)
            {
                throw std::runtime_error(
                    "unable to create the isolated DAT test directory: " +
                    lastError.message());
            }

            throw std::runtime_error(
                "unable to create a unique isolated DAT test directory");
        }


        TemporaryDirectory(const TemporaryDirectory&) = delete;
        TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;


        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }


        [[nodiscard]] std::filesystem::path nextPath(
            std::string_view stem)
        {
            ++counter_;
            return path_ /
                (std::string(stem) + '-' + std::to_string(counter_) +
                    ".dat");
        }


        [[nodiscard]] const std::filesystem::path& path() const noexcept
        {
            return path_;
        }

    private:
        std::filesystem::path path_;
        std::uint32_t counter_{};
    };


    [[nodiscard]] DataBytes byteString(std::string_view text)
    {
        DataBytes result;
        result.reserve(text.size());

        for (const char character : text)
        {
            result.push_back(static_cast<std::byte>(
                static_cast<unsigned char>(character)));
        }

        return result;
    }


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


    [[nodiscard]] std::uint16_t readU16Le(
        std::span<const std::byte> bytes,
        std::size_t offset)
    {
        if (offset + 2 > bytes.size())
        {
            throw std::runtime_error("test attempted an out-of-range u16 read");
        }

        return
            static_cast<std::uint16_t>(
                std::to_integer<std::uint8_t>(bytes[offset])) |
            static_cast<std::uint16_t>(
                std::to_integer<std::uint8_t>(bytes[offset + 1]) << 8U);
    }


    [[nodiscard]] std::uint32_t readU32Le(
        std::span<const std::byte> bytes,
        std::size_t offset)
    {
        if (offset + 4 > bytes.size())
        {
            throw std::runtime_error("test attempted an out-of-range u32 read");
        }

        return
            static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes[offset])) |
            (static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes[offset + 1])) << 8U) |
            (static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes[offset + 2])) << 16U) |
            (static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes[offset + 3])) << 24U);
    }


    void writeU16Le(
        DataBytes& bytes,
        std::size_t offset,
        std::uint16_t value)
    {
        if (offset + 2 > bytes.size())
        {
            throw std::runtime_error("test attempted an out-of-range u16 write");
        }

        bytes[offset] = static_cast<std::byte>(value & 0xFFU);
        bytes[offset + 1] =
            static_cast<std::byte>((value >> 8U) & 0xFFU);
    }


    void writeU32Le(
        DataBytes& bytes,
        std::size_t offset,
        std::uint32_t value)
    {
        if (offset + 4 > bytes.size())
        {
            throw std::runtime_error("test attempted an out-of-range u32 write");
        }

        bytes[offset] = static_cast<std::byte>(value & 0xFFU);
        bytes[offset + 1] =
            static_cast<std::byte>((value >> 8U) & 0xFFU);
        bytes[offset + 2] =
            static_cast<std::byte>((value >> 16U) & 0xFFU);
        bytes[offset + 3] =
            static_cast<std::byte>((value >> 24U) & 0xFFU);
    }


    [[nodiscard]] std::size_t recordOffset(DataTag tag)
    {
        return LegacyDataArchive::PhysicalHeaderSize +
            static_cast<std::size_t>(tag) *
                LegacyDataArchive::PhysicalIndexRecordSize;
    }


    [[nodiscard]] DataBytes makeIndexTable(
        std::initializer_list<std::pair<std::uint32_t, DataTag>> entries)
    {
        DataBytes bytes;
        bytes.reserve(entries.size() * DataIndexTable::PhysicalEntrySize);

        for (const auto [key, tag] : entries)
        {
            appendU32Le(bytes, key);
            appendU16Le(bytes, tag);
        }

        return bytes;
    }


    [[nodiscard]] std::vector<ArchiveBuildItem> makeFixtureItems()
    {
        return
        {
            {
                LegacyDataType::IndexTable,
                makeIndexTable(
                {
                    { 0U, DataTag{ 1U } },
                    { 7U, DataTag{ 2U } },
                    { 42U, DataTag{ 2U } }
                })
            },
            { LegacyDataType::String, byteString("fallback") },
            { LegacyDataType::String, byteString("answer") },
            {},
            { LegacyDataType::Chunky, byteString("synthetic chunky payload") }
        };
    }


    [[nodiscard]] ArchiveOpenOptions verifyChecksums()
    {
        ArchiveOpenOptions options;
        options.checksumPolicy = ChecksumPolicy::Verify;
        return options;
    }


    [[nodiscard]] ArchiveOpenOptions ignoreChecksums()
    {
        ArchiveOpenOptions options;
        options.checksumPolicy = ChecksumPolicy::Ignore;
        return options;
    }


    void writeBytes(
        const std::filesystem::path& path,
        std::span<const std::byte> bytes)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);

        if (!output)
        {
            throw std::runtime_error(
                "unable to create synthetic DAT fixture " + path.string());
        }

        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));

        if (!output)
        {
            throw std::runtime_error(
                "unable to write synthetic DAT fixture " + path.string());
        }
    }


    [[nodiscard]] DataBytes readBytes(const std::filesystem::path& path)
    {
        const auto size = std::filesystem::file_size(path);
        DataBytes bytes(static_cast<std::size_t>(size));
        std::ifstream input(path, std::ios::binary);

        if (!input)
        {
            throw std::runtime_error(
                "unable to reopen synthetic DAT fixture " + path.string());
        }

        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));

        if (input.gcount() != static_cast<std::streamsize>(bytes.size()))
        {
            throw std::runtime_error(
                "unable to reread complete synthetic DAT fixture " +
                path.string());
        }

        return bytes;
    }


    [[nodiscard]] std::filesystem::path writeFixture(
        TemporaryDirectory& temporary,
        std::string_view stem,
        std::span<const std::byte> bytes)
    {
        auto path = temporary.nextPath(stem);
        writeBytes(path, bytes);
        return path;
    }


    void expectOpenError(
        TemporaryDirectory& temporary,
        std::string_view stem,
        std::span<const std::byte> bytes,
        DataErrorCode code,
        std::string_view description,
        ArchiveOpenOptions options = ignoreChecksums())
    {
        const auto path = writeFixture(temporary, stem, bytes);
        const auto opened = LegacyDataArchive::open(path, 9U, options);
        expectError(opened, code, description);
    }


    void testBuilderAndPhysicalLayout(TemporaryDirectory& temporary)
    {
        const auto items = makeFixtureItems();
        ArchiveBuildOptions options;
        options.patchMajor = 4U;
        options.patchMinor = 11U;
        options.compressionLevel = 8;

        const auto first = requireValue(
            monopoly::data::buildLegacyDataArchive(items, options),
            "build deterministic fixture A");
        const auto second = requireValue(
            monopoly::data::buildLegacyDataArchive(items, options),
            "build deterministic fixture B");

        expect(first == second, "builder output is byte-for-byte deterministic");
        expect(first.size() > LegacyDataArchive::PhysicalHeaderSize,
            "builder emits header, index and compressed payloads");
        expect(
            first[0] == std::byte{ 'A' } &&
                first[1] == std::byte{ 'r' } &&
                first[2] == std::byte{ 't' } &&
                first[3] == std::byte{ 'e' } &&
                first[4] == std::byte{ 'c' } &&
                first[5] == std::byte{ 'h' } &&
                first[6] == std::byte{ 0 },
            "builder writes the seven-byte Artech\\0 signature");
        expect(
            readU16Le(first, 8U) ==
                    LegacyDataArchive::SupportedVersionMajor &&
                readU16Le(first, 10U) ==
                    LegacyDataArchive::SupportedVersionMinor,
            "builder writes DAT version 2.3 in little-endian fields");
        expect(
            readU16Le(first, 12U) == 4U &&
                readU16Le(first, 14U) == 11U,
            "builder preserves caller-supplied patch version fields");
        expect(readU32Le(first, 20U) == items.size(),
            "builder writes the exact physical index count");
        expect(readU32Le(first, 24U) != 0U,
            "builder stores a CRC-32 over bytes following the header");

        const auto emptyRecord = recordOffset(3U);
        expect(
            readU32Le(first, emptyRecord) == 0U &&
                readU32Le(first, emptyRecord + 4U) == 0U &&
                readU32Le(first, emptyRecord + 8U) == 0U &&
                readU32Le(first, emptyRecord + 12U) == 0U,
            "builder preserves an all-zero sparse DataTag slot");

        const auto outputPath = temporary.nextPath("writer-roundtrip");
        requireSuccess(
            monopoly::data::writeLegacyDataArchive(
                outputPath,
                items,
                options),
            "write deterministic fixture");
        expect(readBytes(outputPath) == first,
            "file writer preserves the exact deterministic archive bytes");

        const std::span<const ArchiveBuildItem> noItems;
        expectError(
            monopoly::data::buildLegacyDataArchive(noItems),
            DataErrorCode::ArchiveBuildFailed,
            "builder rejects a zero-entry DAT index");

        auto invalidLevel = options;
        invalidLevel.compressionLevel = -1;
        expectError(
            monopoly::data::buildLegacyDataArchive(items, invalidLevel),
            DataErrorCode::ArchiveBuildFailed,
            "builder rejects a compression level below zlib's range");
        invalidLevel.compressionLevel = 10;
        expectError(
            monopoly::data::buildLegacyDataArchive(items, invalidLevel),
            DataErrorCode::ArchiveBuildFailed,
            "builder rejects a compression level above zlib's range");

        const std::array unknownPayload
        {
            ArchiveBuildItem
            {
                LegacyDataType::Unknown,
                byteString("present")
            }
        };
        expectError(
            monopoly::data::buildLegacyDataArchive(unknownPayload),
            DataErrorCode::ArchiveBuildFailed,
            "builder rejects a present item with Unknown type");

        const std::array emptyTypedPayload
        {
            ArchiveBuildItem { LegacyDataType::String, {} }
        };
        expectError(
            monopoly::data::buildLegacyDataArchive(emptyTypedPayload),
            DataErrorCode::ArchiveBuildFailed,
            "builder rejects a typed item with an empty payload");

        std::vector<ArchiveBuildItem> tooManyItems(
            LegacyDataArchive::MaximumItemCount + 1U);
        expectError(
            monopoly::data::buildLegacyDataArchive(tooManyItems),
            DataErrorCode::ArchiveBuildFailed,
            "builder rejects an index larger than the DataTag space");

        expectError(
            monopoly::data::writeLegacyDataArchive(
                temporary.path(),
                items),
            DataErrorCode::WriteFailed,
            "file writer reports an unwritable directory target");
        expectError(
            monopoly::data::writeLegacyDataArchive(
                temporary.nextPath("invalid-writer-input"),
                noItems),
            DataErrorCode::ArchiveBuildFailed,
            "file writer propagates builder validation failures");
    }


    void testSuccessfulArchiveAndCache(TemporaryDirectory& temporary)
    {
        const auto items = makeFixtureItems();
        ArchiveBuildOptions buildOptions;
        buildOptions.patchMajor = 7U;
        buildOptions.patchMinor = 9U;
        const auto bytes = requireValue(
            monopoly::data::buildLegacyDataArchive(items, buildOptions),
            "build cache fixture");
        const auto path = writeFixture(temporary, "cache", bytes);
        auto archive = requireValue(
            LegacyDataArchive::open(path, 9U, verifyChecksums()),
            "open verified cache fixture");

        expect(archive->isOpen(), "freshly opened archive reports open state");
        expect(archive->group() == 9U, "archive preserves its mounted group");
        expect(archive->path() == path, "archive preserves its source path");
        expect(
            archive->version().major == 2U &&
                archive->version().minor == 3U &&
                archive->version().patchMajor == 7U &&
                archive->version().patchMinor == 9U,
            "archive exposes all four version fields");
        expect(archive->storedChecksum() == readU32Le(bytes, 24U),
            "archive exposes the stored CRC-32 without rewriting it");
        expect(archive->itemCount() == items.size(),
            "archive exposes the complete sparse index size");

        const auto indexMetadata = archive->metadata(0U);
        expect(
            indexMetadata &&
                indexMetadata->tag == 0U &&
                indexMetadata->type == LegacyDataType::IndexTable &&
                indexMetadata->present(),
            "metadata identifies the tag-zero IndexTable item");
        const auto emptyMetadata = archive->metadata(3U);
        expect(emptyMetadata && !emptyMetadata->present(),
            "metadata exposes an empty sparse slot without loading it");
        expectError(
            archive->metadata(5U),
            DataErrorCode::TagOutOfRange,
            "metadata rejects a DataTag equal to itemCount");
        expectError(
            archive->load(3U),
            DataErrorCode::EmptyItem,
            "loader rejects an all-zero sparse slot");
        expectError(
            archive->load(std::numeric_limits<DataTag>::max()),
            DataErrorCode::TagOutOfRange,
            "loader rejects an out-of-range DataTag");

        const auto firstLoad = requireValue(
            archive->load(2U),
            "load answer item first time");
        expect(*firstLoad == items[2].payload,
            "zlib round-trip restores exact uncompressed payload bytes");
        expect(archive->cachedItemCount() == 1U,
            "first load installs exactly one strong cache entry");

        const auto secondLoad = requireValue(
            archive->load(2U),
            "load answer item from cache");
        expect(firstLoad.get() == secondLoad.get(),
            "repeated load returns the same cached allocation");

        const auto removed = requireValue(
            archive->unload(2U),
            "unload cached answer item");
        expect(removed, "unload reports that a cached entry was removed");
        expect(archive->cachedItemCount() == 0U,
            "unload releases only the archive cache ownership");
        expect(*firstLoad == items[2].payload,
            "caller lease survives unload with its bytes intact");
        expect(
            !requireValue(
                archive->unload(2U),
                "unload already uncached answer item"),
            "second unload reports that no cache entry existed");

        const auto reloaded = requireValue(
            archive->load(2U),
            "reload answer after eviction");
        expect(reloaded.get() != firstLoad.get(),
            "reload creates a distinct allocation while the old lease lives");
        archive->clearCache();
        expect(archive->cachedItemCount() == 0U,
            "clearCache releases every archive-owned payload");
        expect(*reloaded == items[2].payload,
            "caller lease survives clearCache with its bytes intact");

        archive->close();
        archive->close();
        expect(!archive->isOpen(), "close is idempotent and reports closed state");
        expect(*firstLoad == items[2].payload && *reloaded == items[2].payload,
            "all caller leases survive archive close");
        expectError(
            archive->load(2U),
            DataErrorCode::ArchiveClosed,
            "closed archive rejects subsequent loads");
    }


    void testHeaderIndexAndRangeValidation(TemporaryDirectory& temporary)
    {
        const auto baseline = requireValue(
            monopoly::data::buildLegacyDataArchive(makeFixtureItems()),
            "build mutation baseline");

        for (const std::size_t size : { 0U, 7U, 27U })
        {
            const auto truncated = std::span<const std::byte>(baseline).first(size);
            expectOpenError(
                temporary,
                "header-truncated",
                truncated,
                DataErrorCode::HeaderTruncated,
                "reader rejects a truncated physical DAT header");
        }

        auto badSignature = baseline;
        badSignature[5] = std::byte{ 'x' };
        expectOpenError(
            temporary,
            "signature",
            badSignature,
            DataErrorCode::InvalidSignature,
            "reader rejects a corrupted Artech signature byte");

        auto uncheckedSignaturePadding = baseline;
        uncheckedSignaturePadding[7] = std::byte{ 0xA5 };
        const auto paddingPath = writeFixture(
            temporary,
            "signature-padding",
            uncheckedSignaturePadding);
        expect(
            LegacyDataArchive::open(
                paddingPath,
                9U,
                verifyChecksums()).has_value(),
            "reader preserves L_Data behavior by ignoring signature byte 7");

        auto reservedFields = baseline;
        writeU16Le(reservedFields, 16U, 0x1234U);
        writeU16Le(reservedFields, 18U, 0x5678U);
        const auto reservedPath = writeFixture(
            temporary,
            "reserved-fields",
            reservedFields);
        expect(
            LegacyDataArchive::open(
                reservedPath,
                9U,
                verifyChecksums()).has_value(),
            "reader ignores the two reserved header fields like L_Data");

        auto badMajor = baseline;
        writeU16Le(badMajor, 8U, 3U);
        expectOpenError(
            temporary,
            "version-major",
            badMajor,
            DataErrorCode::UnsupportedVersion,
            "reader rejects an unsupported DAT major version");
        auto badMinor = baseline;
        writeU16Le(badMinor, 10U, 2U);
        expectOpenError(
            temporary,
            "version-minor",
            badMinor,
            DataErrorCode::UnsupportedVersion,
            "reader rejects an unsupported DAT minor version");

        auto zeroCount = baseline;
        writeU32Le(zeroCount, 20U, 0U);
        expectOpenError(
            temporary,
            "zero-index",
            zeroCount,
            DataErrorCode::InvalidHeader,
            "reader rejects a zero-entry DAT index");
        auto excessiveCount = baseline;
        writeU32Le(
            excessiveCount,
            20U,
            static_cast<std::uint32_t>(
                LegacyDataArchive::MaximumItemCount + 1U));
        expectOpenError(
            temporary,
            "huge-index",
            excessiveCount,
            DataErrorCode::IndexTooLarge,
            "reader rejects an index outside the 16-bit DataTag space");

        DataBytes headerOnly(
            baseline.begin(),
            baseline.begin() +
                static_cast<std::ptrdiff_t>(
                    LegacyDataArchive::PhysicalHeaderSize));
        writeU32Le(headerOnly, 20U, 1U);
        expectOpenError(
            temporary,
            "missing-index-record",
            headerOnly,
            DataErrorCode::IndexTruncated,
            "reader rejects a header whose declared index is absent");

        auto truncatedIndex = baseline;
        const auto dataStart =
            LegacyDataArchive::PhysicalHeaderSize +
            makeFixtureItems().size() *
                LegacyDataArchive::PhysicalIndexRecordSize;
        truncatedIndex.resize(dataStart - 1U);
        expectOpenError(
            temporary,
            "truncated-index",
            truncatedIndex,
            DataErrorCode::IndexTruncated,
            "reader rejects an index truncated by one physical byte");

        auto badType = baseline;
        writeU32Le(
            badType,
            recordOffset(0U) + 8U,
            static_cast<std::uint32_t>(LegacyDataType::MeshX) + 1U);
        expectOpenError(
            temporary,
            "bad-type",
            badType,
            DataErrorCode::InvalidItemType,
            "reader rejects a type outside LE_DATA_DataType");

        auto offsetBeforePayload = baseline;
        writeU32Le(
            offsetBeforePayload,
            recordOffset(0U),
            static_cast<std::uint32_t>(dataStart - 1U));
        expectOpenError(
            temporary,
            "offset-before-payload",
            offsetBeforePayload,
            DataErrorCode::InvalidItemRange,
            "reader rejects an item offset overlapping the DAT index");

        auto zeroUncompressed = baseline;
        writeU32Le(zeroUncompressed, recordOffset(0U) + 4U, 0U);
        expectOpenError(
            temporary,
            "zero-uncompressed",
            zeroUncompressed,
            DataErrorCode::InvalidItemRange,
            "reader rejects a present item with zero uncompressed size");

        auto zeroCompressed = baseline;
        writeU32Le(zeroCompressed, recordOffset(0U) + 12U, 0U);
        expectOpenError(
            temporary,
            "zero-compressed",
            zeroCompressed,
            DataErrorCode::InvalidItemRange,
            "reader rejects a present item with zero compressed size");

        auto unknownPresent = baseline;
        writeU32Le(
            unknownPresent,
            recordOffset(0U) + 8U,
            static_cast<std::uint32_t>(LegacyDataType::Unknown));
        expectOpenError(
            temporary,
            "unknown-present",
            unknownPresent,
            DataErrorCode::InvalidItemRange,
            "reader rejects an Unknown item carrying nonzero range fields");

        auto signedOverflow = baseline;
        writeU32Le(signedOverflow, recordOffset(0U), 0x80000000U);
        expectOpenError(
            temporary,
            "signed-overflow",
            signedOverflow,
            DataErrorCode::InvalidItemRange,
            "reader rejects offsets outside the original signed Win32 field");

        auto itemPastEnd = baseline;
        writeU32Le(
            itemPastEnd,
            recordOffset(0U) + 12U,
            static_cast<std::uint32_t>(baseline.size()));
        expectOpenError(
            temporary,
            "item-past-end",
            itemPastEnd,
            DataErrorCode::InvalidItemRange,
            "reader rejects a compressed range extending past end of file");

        auto truncatedPayload = baseline;
        truncatedPayload.pop_back();
        expectOpenError(
            temporary,
            "truncated-payload",
            truncatedPayload,
            DataErrorCode::InvalidItemRange,
            "reader rejects a payload truncated after a valid index");

        auto maximumOptions = ignoreChecksums();
        maximumOptions.maximumUncompressedItemSize = 8U;
        expectOpenError(
            temporary,
            "maximum-item-size",
            baseline,
            DataErrorCode::InvalidItemRange,
            "reader enforces the configured uncompressed-size ceiling",
            maximumOptions);

        const auto validPath = writeFixture(
            temporary,
            "invalid-group-source",
            baseline);
        expectError(
            LegacyDataArchive::open(validPath, 0U),
            DataErrorCode::InvalidGroup,
            "reader rejects reserved group zero before mounting");
        expectError(
            LegacyDataArchive::open(
                temporary.nextPath("missing-file"),
                9U),
            DataErrorCode::FileOpenFailed,
            "reader reports a missing DAT file");
    }


    void testChecksumPolicies(TemporaryDirectory& temporary)
    {
        auto bytes = requireValue(
            monopoly::data::buildLegacyDataArchive(makeFixtureItems()),
            "build checksum fixture");
        const auto originalChecksum = readU32Le(bytes, 24U);
        writeU32Le(bytes, 24U, originalChecksum ^ 0xFFFFFFFFU);
        const auto path = writeFixture(temporary, "checksum", bytes);

        expectError(
            LegacyDataArchive::open(path, 9U, verifyChecksums()),
            DataErrorCode::ChecksumMismatch,
            "Verify policy rejects a mismatched stored CRC-32");

        auto ignored = LegacyDataArchive::open(path, 9U, ignoreChecksums());
        expect(ignored.has_value(),
            "Ignore policy accepts an archive with an untrusted stored CRC-32");

        if (ignored)
        {
            const auto payload = (*ignored)->load(2U);
            expect(
                payload && **payload == makeFixtureItems()[2].payload,
                "Ignore policy does not alter otherwise valid item decoding");
        }

        expect(
            LegacyDataArchive::open(path, 9U).has_value(),
            "default open policy treats the historically unchecked CRC as advisory");
    }


    void testZlibAndSizeFailures(TemporaryDirectory& temporary)
    {
        DataBytes payload;
        payload.reserve(256U);

        for (std::uint32_t value = 0; value < 256U; ++value)
        {
            payload.push_back(static_cast<std::byte>(value & 0x1FU));
        }

        const std::array items
        {
            ArchiveBuildItem { LegacyDataType::Native, payload }
        };
        const auto baseline = requireValue(
            monopoly::data::buildLegacyDataArchive(items),
            "build zlib mutation fixture");
        const auto itemRecord = recordOffset(0U);
        const auto payloadOffset = readU32Le(baseline, itemRecord);
        const auto uncompressedSize = readU32Le(baseline, itemRecord + 4U);
        const auto compressedSize = readU32Le(baseline, itemRecord + 12U);

        auto corruptedStream = baseline;
        corruptedStream[payloadOffset] ^= std::byte{ 0xFF };
        const auto corruptPath = writeFixture(
            temporary,
            "zlib-corrupt",
            corruptedStream);
        auto corruptArchive = requireValue(
            LegacyDataArchive::open(
                corruptPath,
                2U,
                ignoreChecksums()),
            "open corrupt zlib fixture without CRC verification");
        expectError(
            corruptArchive->load(0U),
            DataErrorCode::DecompressionFailed,
            "loader reports a corrupted zlib stream");

        auto truncatedStream = baseline;
        writeU32Le(
            truncatedStream,
            itemRecord + 12U,
            compressedSize - 1U);
        const auto truncatedPath = writeFixture(
            temporary,
            "zlib-truncated",
            truncatedStream);
        auto truncatedArchive = requireValue(
            LegacyDataArchive::open(
                truncatedPath,
                2U,
                ignoreChecksums()),
            "open truncated zlib stream fixture");
        expectError(
            truncatedArchive->load(0U),
            DataErrorCode::DecompressionFailed,
            "loader rejects a zlib stream shortened by its index size");

        auto oversizedDeclaration = baseline;
        writeU32Le(
            oversizedDeclaration,
            itemRecord + 4U,
            uncompressedSize + 1U);
        const auto oversizedPath = writeFixture(
            temporary,
            "inflate-size-large",
            oversizedDeclaration);
        auto oversizedArchive = requireValue(
            LegacyDataArchive::open(
                oversizedPath,
                2U,
                ignoreChecksums()),
            "open oversized output declaration fixture");
        expectError(
            oversizedArchive->load(0U),
            DataErrorCode::DecompressedSizeMismatch,
            "loader rejects an inflated size smaller than the index declaration");

        auto undersizedDeclaration = baseline;
        writeU32Le(
            undersizedDeclaration,
            itemRecord + 4U,
            uncompressedSize - 1U);
        const auto undersizedPath = writeFixture(
            temporary,
            "inflate-size-small",
            undersizedDeclaration);
        auto undersizedArchive = requireValue(
            LegacyDataArchive::open(
                undersizedPath,
                2U,
                ignoreChecksums()),
            "open undersized output declaration fixture");
        expectError(
            undersizedArchive->load(0U),
            DataErrorCode::DecompressionFailed,
            "loader reports zlib buffer exhaustion for an undersized output");

        auto trailingCompressedByte = baseline;
        trailingCompressedByte.push_back(std::byte{ 0xA5 });
        writeU32Le(
            trailingCompressedByte,
            itemRecord + 12U,
            compressedSize + 1U);
        const auto trailingPath = writeFixture(
            temporary,
            "zlib-trailing-byte",
            trailingCompressedByte);
        auto trailingArchive = requireValue(
            LegacyDataArchive::open(
                trailingPath,
                2U,
                ignoreChecksums()),
            "open zlib stream with indexed trailing byte");
        expectError(
            trailingArchive->load(0U),
            DataErrorCode::DecompressedSizeMismatch,
            "loader rejects a compressed range not consumed exactly by zlib");
    }


    void testDataIndexTable()
    {
        const auto aliasedBytes = makeIndexTable(
        {
            { 0U, DataTag{ 0U } },
            { 7U, DataTag{ 2U } },
            { 42U, DataTag{ 2U } },
            { 1000U, DataTag{ 9U } }
        });
        const auto table = DataIndexTable::parse(aliasedBytes);
        expect(table.has_value(),
            "sorted six-byte index entries parse successfully");

        if (table)
        {
            const auto entries = table->entries();
            expect(entries.size() == 4U,
                "parsed index preserves every logical key");
            expect(
                entries[0].indexValue == 0U && entries[0].dataTag == 0U &&
                    entries[3].indexValue == 1000U &&
                    entries[3].dataTag == 9U,
                "parsed index preserves little-endian keys and DataTags");
            expect(table->find(0U) == 0U,
                "index lookup permits DataTag zero");
            expect(table->find(7U) == 2U && table->find(42U) == 2U,
                "distinct sorted keys may intentionally alias one DataTag");
            expect(!table->find(8U).has_value(),
                "index lookup returns no tag for an absent logical key");
        }

        const DataBytes empty;
        expectError(
            DataIndexTable::parse(empty),
            DataErrorCode::InvalidIndexTable,
            "index parser rejects an empty table");

        DataBytes fiveBytes(5U, std::byte{ 0 });
        expectError(
            DataIndexTable::parse(fiveBytes),
            DataErrorCode::InvalidIndexTable,
            "index parser rejects a truncated entry shorter than six bytes");
        DataBytes sevenBytes(7U, std::byte{ 0 });
        expectError(
            DataIndexTable::parse(sevenBytes),
            DataErrorCode::InvalidIndexTable,
            "index parser rejects trailing bytes after a complete entry");

        const auto unsorted = makeIndexTable(
        {
            { 10U, DataTag{ 1U } },
            { 9U, DataTag{ 2U } }
        });
        expectError(
            DataIndexTable::parse(unsorted),
            DataErrorCode::UnsortedIndexTable,
            "index parser rejects descending logical keys");
        const auto duplicate = makeIndexTable(
        {
            { 10U, DataTag{ 1U } },
            { 10U, DataTag{ 2U } }
        });
        expectError(
            DataIndexTable::parse(duplicate),
            DataErrorCode::DuplicateIndexKey,
            "index parser rejects duplicate keys even when tags differ");
    }


    void testRegistryAndIndexedLookup(TemporaryDirectory& temporary)
    {
        auto items = makeFixtureItems();
        items[4].payload =
        {
            std::byte{ 6 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 7 },
            std::byte{ 'o' }, std::byte{ 'k' }
        };
        const auto bytes = requireValue(
            monopoly::data::buildLegacyDataArchive(items),
            "build registry fixture");
        const auto path = writeFixture(temporary, "registry", bytes);

        DataBankRegistry registry;
        auto mountedText = requireValue(
            registry.mount(path, 9U, verifyChecksums()),
            "mount text group");
        expect(registry.mountedCount() == 1U,
            "registry counts the first mounted group");
        expectError(
            registry.mount(path, 9U, verifyChecksums()),
            DataErrorCode::DuplicateGroup,
            "registry rejects a duplicate group mount");

        auto mountedDialog = requireValue(
            registry.mount(path, 10U, verifyChecksums()),
            "mount second group from same synthetic archive");
        expect(registry.mountedCount() == 2U,
            "registry supports the same file mounted under distinct groups");
        expect(
            requireValue(registry.archive(9U), "retrieve mounted text group") ==
                mountedText,
            "registry returns the exact shared archive instance");
        expectError(
            registry.archive(8U),
            DataErrorCode::GroupNotMounted,
            "registry reports an absent group");

        const auto answerId = monopoly::data::packDataId(9U, 2U);
        const auto answerMetadata = registry.metadata(answerId);
        expect(
            answerMetadata && answerMetadata->type == LegacyDataType::String,
            "registry routes metadata by the DataId high-word group");
        const auto answerLease = requireValue(
            registry.load(answerId),
            "load registry answer item");
        expect(*answerLease == items[2].payload,
            "registry routes payload loads by group and tag");
        expectError(
            registry.metadata(monopoly::data::packDataId(9U, 99U)),
            DataErrorCode::TagOutOfRange,
            "registry preserves archive tag-range errors");
        expectError(
            registry.load(monopoly::data::packDataId(8U, 0U)),
            DataErrorCode::GroupNotMounted,
            "registry rejects loads for an unmounted group");

        auto chunkReader = requireValue(
            monopoly::data::openLegacyChunkReader(
                registry,
                monopoly::data::packDataId(9U, 4U)),
            "open DataChunky item through registry");
        expectError(
            monopoly::data::openLegacyChunkReader(
                registry,
                monopoly::data::packDataId(9U, 1U)),
            DataErrorCode::TypeMismatch,
            "chunk adapter rejects non-DataChunky items");

        const auto indexId = monopoly::data::packDataId(9U, 0U);
        const auto key7 = monopoly::data::lookupIndexedDataId(
            registry,
            indexId,
            7U);
        const auto key42 = monopoly::data::lookupIndexedDataId(
            registry,
            indexId,
            42U);
        expect(
            key7 && key42 &&
                *key7 == answerId && *key42 == answerId,
            "indexed lookup reapplies the source group and permits tag aliases");
        expectError(
            monopoly::data::lookupIndexedDataId(
                registry,
                indexId,
                999U),
            DataErrorCode::IndexedItemNotFound,
            "indexed lookup reports an absent logical key");
        expectError(
            monopoly::data::lookupIndexedDataId(
                registry,
                monopoly::data::packDataId(9U, 1U),
                0U),
            DataErrorCode::TypeMismatch,
            "indexed lookup rejects a non-IndexTable source item");

        requireSuccess(registry.unmount(9U), "unmount text group");
        expect(registry.mountedCount() == 1U,
            "unmount removes exactly one registry group");
        expect(!mountedText->isOpen(),
            "unmount closes the removed archive instance");
        expectError(
            registry.archive(9U),
            DataErrorCode::GroupNotMounted,
            "unmounted group is no longer discoverable");
        expectError(
            mountedText->load(2U),
            DataErrorCode::ArchiveClosed,
            "held archive pointer observes unmount closure");
        expect(*answerLease == items[2].payload,
            "payload lease remains valid after registry unmount");
        const auto chunk = chunkReader.descend();
        const auto chunkPayload = chunkReader.map(2U);
        expect(
            chunk && chunk->id == 7 && chunkPayload &&
                (*chunkPayload)[0] == std::byte{ 'o' } &&
                (*chunkPayload)[1] == std::byte{ 'k' },
            "chunk reader lease remains valid after its DAT group is unmounted");
        expectError(
            registry.unmount(9U),
            DataErrorCode::GroupNotMounted,
            "registry rejects repeated unmount of the same group");

        registry.clear();
        expect(registry.mountedCount() == 0U,
            "registry clear removes every remaining group");
        expect(!mountedDialog->isOpen(),
            "registry clear closes each removed archive");

        expectError(
            registry.mount(path, 0U),
            DataErrorCode::InvalidGroup,
            "registry propagates reserved group-zero validation");
        expectError(
            registry.mount(temporary.nextPath("missing-registry"), 11U),
            DataErrorCode::FileOpenFailed,
            "registry propagates archive file-open failures");
    }
}


int main()
{
    std::cout
        << "Monopoly legacy DAT archive tests\n"
        << "=================================\n";

    try
    {
        TemporaryDirectory temporary;
        testBuilderAndPhysicalLayout(temporary);
        testSuccessfulArchiveAndCache(temporary);
        testHeaderIndexAndRangeValidation(temporary);
        testChecksumPolicies(temporary);
        testZlibAndSizeFailures(temporary);
        testDataIndexTable();
        testRegistryAndIndexedLookup(temporary);
    }
    catch (const std::exception& exception)
    {
        ++failures;
        std::cerr << "[FAIL] unexpected test harness error: "
                  << exception.what() << '\n';
    }

    if (failures != 0)
    {
        std::cerr << failures << " legacy DAT archive test(s) failed.\n";
        return 1;
    }

    std::cout << "All legacy DAT archive tests passed.\n";
    return 0;
}
