#pragma once

#include "DataBanks.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace monopoly::data
{
    // Ordre et valeurs de LE_DATA_DataType dans Source/artlib/L_Data.h.
    enum class LegacyDataType : std::uint8_t
    {
        Unknown = 0,
        Bitmap = 1,
        DopeTable = 2,
        Uap = 3,
        Native = 4,
        GbmTexture = 5,
        GbmPicture = 6,
        GenericBitmap = 7,
        Wave = 8,
        String = 9,
        UserCreated1 = 10,
        IndexTable = 11,
        Chunky = 12,
        TextureArray = 13,
        Model3D = 14,
        Pose3D = 15,
        Hmd = 16,
        MeshX = 17
    };


    [[nodiscard]] std::string_view legacyDataTypeName(
        LegacyDataType type) noexcept;


    enum class DataErrorCode
    {
        None,
        InvalidGroup,
        InvalidLanguage,
        NoActiveLanguage,
        DuplicateGroup,
        GroupNotMounted,
        ArchiveClosed,
        FileOpenFailed,
        FileSizeFailed,
        HeaderTruncated,
        InvalidSignature,
        UnsupportedVersion,
        InvalidHeader,
        IndexTooLarge,
        IndexTruncated,
        InvalidItemType,
        InvalidItemRange,
        ChecksumMismatch,
        TagOutOfRange,
        EmptyItem,
        ReadFailed,
        DecompressionFailed,
        DecompressedSizeMismatch,
        ArchiveBuildFailed,
        WriteFailed,
        InvalidIndexTable,
        UnsortedIndexTable,
        DuplicateIndexKey,
        IndexedItemNotFound,
        TypeMismatch,
        InvalidUtf16,
        InvalidBoardEdition,
        ResourcePathInvalid,
        ResourceNotFound,
        ResourcePathAmbiguous,
        ResourcePathFailed
    };


    struct DataError
    {
        DataErrorCode code{ DataErrorCode::None };
        std::filesystem::path path;
        std::optional<DataTag> tag;
        std::string detail;
    };


    [[nodiscard]] std::string_view dataErrorCodeName(
        DataErrorCode code) noexcept;


    struct ArchiveVersion
    {
        std::uint16_t major{};
        std::uint16_t minor{};
        std::uint16_t patchMajor{};
        std::uint16_t patchMinor{};
    };


    struct ArchiveItemMetadata
    {
        DataTag tag{};
        LegacyDataType type{ LegacyDataType::Unknown };
        std::uint32_t offset{};
        std::uint32_t uncompressedSize{};
        std::uint32_t compressedSize{};

        [[nodiscard]] constexpr bool present() const noexcept
        {
            return
                type != LegacyDataType::Unknown ||
                offset != 0 ||
                uncompressedSize != 0 ||
                compressedSize != 0;
        }
    };


    enum class ChecksumPolicy
    {
        Verify,
        Ignore
    };


    struct ArchiveOpenOptions
    {
        // L_Data conservait et journalisait le CRC sans jamais le verifier.
        // La convention annoncee par le header reste disponible comme audit
        // opt-in, mais ne doit pas rendre le chemin runtime plus strict que
        // l'original tant qu'aucune banque retail n'a permis de la confirmer.
        ChecksumPolicy checksumPolicy{ ChecksumPolicy::Ignore };
        std::uint32_t maximumUncompressedItemSize{ 512U * 1024U * 1024U };
    };


    using DataBytes = std::vector<std::byte>;
    using SharedDataBytes = std::shared_ptr<const DataBytes>;


    class LegacyDataArchive final
    {
    public:
        static constexpr std::size_t PhysicalHeaderSize = 28;
        static constexpr std::size_t PhysicalIndexRecordSize = 16;
        static constexpr std::uint16_t SupportedVersionMajor = 2;
        static constexpr std::uint16_t SupportedVersionMinor = 3;
        static constexpr std::size_t MaximumItemCount = 65'536;

        LegacyDataArchive(const LegacyDataArchive&) = delete;
        LegacyDataArchive& operator=(const LegacyDataArchive&) = delete;
        LegacyDataArchive(LegacyDataArchive&&) = delete;
        LegacyDataArchive& operator=(LegacyDataArchive&&) = delete;

        ~LegacyDataArchive();

        [[nodiscard]] static std::expected<
            std::shared_ptr<LegacyDataArchive>,
            DataError>
        open(
            const std::filesystem::path& path,
            std::uint16_t group,
            ArchiveOpenOptions options = {});

        void close() noexcept;

        [[nodiscard]] bool isOpen() const noexcept;
        [[nodiscard]] std::uint16_t group() const noexcept;
        [[nodiscard]] const std::filesystem::path& path() const noexcept;
        [[nodiscard]] ArchiveVersion version() const noexcept;
        [[nodiscard]] std::uint32_t storedChecksum() const noexcept;
        [[nodiscard]] std::size_t itemCount() const noexcept;

        [[nodiscard]] std::expected<ArchiveItemMetadata, DataError>
        metadata(DataTag tag) const;

        [[nodiscard]] std::expected<SharedDataBytes, DataError>
        load(DataTag tag);

        // Retire seulement la possession du cache. Les SharedDataBytes deja
        // remis aux callers restent valides, contrairement aux pointeurs nus
        // de L_Data.
        [[nodiscard]] std::expected<bool, DataError> unload(DataTag tag);
        void clearCache() noexcept;
        [[nodiscard]] std::size_t cachedItemCount() const noexcept;

    private:
        LegacyDataArchive() = default;

        [[nodiscard]] static DataError makeError(
            DataErrorCode code,
            const std::filesystem::path& path,
            std::string detail,
            std::optional<DataTag> tag = std::nullopt);

        mutable std::mutex mutex_;
        std::filesystem::path path_;
        std::ifstream stream_;
        std::uint16_t group_{};
        ArchiveVersion version_{};
        std::uint32_t checksum_{};
        std::uint64_t fileSize_{};
        std::uint32_t maximumUncompressedItemSize_{};
        bool open_{};
        std::vector<ArchiveItemMetadata> items_;
        std::vector<SharedDataBytes> cache_;
    };


    class DataBankRegistry final
    {
    public:
        [[nodiscard]] std::expected<
            std::shared_ptr<LegacyDataArchive>,
            DataError>
        mount(
            const std::filesystem::path& path,
            std::uint16_t group,
            ArchiveOpenOptions options = {});

        [[nodiscard]] std::expected<
            std::shared_ptr<LegacyDataArchive>,
            DataError>
        archive(std::uint16_t group) const;

        [[nodiscard]] std::expected<ArchiveItemMetadata, DataError>
        metadata(DataId id) const;

        [[nodiscard]] std::expected<SharedDataBytes, DataError>
        load(DataId id) const;

        [[nodiscard]] std::expected<void, DataError>
        unmount(std::uint16_t group);

        void clear() noexcept;
        [[nodiscard]] std::size_t mountedCount() const noexcept;

    private:
        mutable std::mutex mutex_;
        std::unordered_map<
            std::uint16_t,
            std::shared_ptr<LegacyDataArchive>> archives_;
    };


    struct DataIndexEntry
    {
        std::uint32_t indexValue{};
        DataTag dataTag{};
    };


    class DataIndexTable final
    {
    public:
        static constexpr std::size_t PhysicalEntrySize = 6;

        [[nodiscard]] static std::expected<DataIndexTable, DataError>
        parse(std::span<const std::byte> bytes);

        [[nodiscard]] std::optional<DataTag> find(
            std::uint32_t indexValue) const noexcept;

        [[nodiscard]] std::span<const DataIndexEntry> entries()
            const noexcept;

    private:
        std::vector<DataIndexEntry> entries_;
    };


    [[nodiscard]] std::expected<DataId, DataError>
    lookupIndexedDataId(
        const DataBankRegistry& registry,
        DataId indexTableId,
        std::uint32_t indexValue);
}
