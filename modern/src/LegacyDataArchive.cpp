#include "LegacyDataArchive.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <list>
#include <sstream>
#include <utility>

namespace monopoly::data
{
    namespace detail
    {
        class RawDataPool;

        // The public shared pointer aliases this lease. Its final release is
        // observable, unlike polling use_count(), so release-to-zero refreshes
        // recency exactly as L_Data's reference-counted unused-item LRU does.
        struct RawDataLease
        {
            SharedDataBytes data;
            std::shared_ptr<RawDataPool> pool;
            std::uint64_t identity{};
            ~RawDataLease();
        };

        class RawDataPool final : public std::enable_shared_from_this<RawDataPool>
        {
            struct Entry
            {
                SharedDataBytes data;
                std::weak_ptr<const DataBytes> lease;
                bool cached{true};
                std::uint64_t identity{};
            };

            using Entries = std::list<Entry>;

        public:
            SharedDataBytes acquire(const std::weak_ptr<const DataBytes>& cached)
            {
                const std::scoped_lock lock(mutex_);
                const auto data = cached.lock();
                if (!data) return {};
                const auto item = find(data.get());
                if (item == entries_.end() || !item->cached) return {};
                entries_.splice(entries_.begin(), entries_, item);
                if (auto lease = item->lease.lock()) return lease;
                return makeLease(*item);
            }

            void reserve(std::size_t size)
            {
                const std::scoped_lock lock(mutex_);
                // L_Data ignores failure to free enough space and attempts
                // allocation anyway. Leases and individual large valid items
                // may exceed this soft threshold; neither is invalidated.
                auto item = entries_.end();
                while (residentBytes_ + reservedBytes_ + size > LegacyRawDataCacheBudget &&
                    item != entries_.begin())
                {
                    --item;
                    if (!item->lease.expired()) continue;
                    residentBytes_ -= item->data->size();
                    item = entries_.erase(item);
                }
                reservedBytes_ += size;
            }

            void cancelReservation(std::size_t size) noexcept
            {
                const std::scoped_lock lock(mutex_);
                reservedBytes_ -= size;
            }

            SharedDataBytes publish(SharedDataBytes data, std::size_t reserved)
            {
                const std::scoped_lock lock(mutex_);
                entries_.push_front({std::move(data), {}, true, ++lastIdentity_});
                SharedDataBytes lease;
                try { lease = makeLease(entries_.front()); }
                catch (...) { entries_.pop_front(); throw; }
                residentBytes_ += entries_.front().data->size();
                reservedBytes_ -= reserved;
                return lease;
            }

            bool remove(const std::weak_ptr<const DataBytes>& cached) noexcept
            {
                const std::scoped_lock lock(mutex_);
                const auto data = cached.lock();
                if (!data) return false;
                const auto item = find(data.get());
                if (item == entries_.end() || !item->cached) return false;
                item->cached = false;
                if (item->lease.expired()) erase(item);
                return true;
            }

            void released(std::uint64_t identity) noexcept
            {
                const std::scoped_lock lock(mutex_);
                const auto item = std::find_if(entries_.begin(), entries_.end(),
                    [identity](const Entry& entry) { return entry.identity == identity; });
                if (item == entries_.end()) return;
                // A concurrent cache hit can already have published a new
                // lease while the old lease destructor awaited this mutex.
                if (!item->lease.expired()) return;
                if (!item->cached) erase(item);
                else entries_.splice(entries_.begin(), entries_, item);
            }

            RawDataCacheStats stats() const noexcept
            {
                const std::scoped_lock lock(mutex_);
                RawDataCacheStats result{residentBytes_, reservedBytes_};
                for (const auto& item : entries_)
                {
                    result.cachedItems += item.cached;
                    result.leasedItems += !item.lease.expired();
                }
                return result;
            }

        private:
            Entries::iterator find(const DataBytes* data) noexcept
            {
                return std::find_if(entries_.begin(), entries_.end(),
                    [data](const Entry& item) { return item.data.get() == data; });
            }

            SharedDataBytes makeLease(Entry& item)
            {
                auto holder = std::make_shared<RawDataLease>();
                holder->data = item.data;
                holder->pool = shared_from_this();
                holder->identity = item.identity;
                SharedDataBytes lease(holder, item.data.get());
                item.lease = lease;
                return lease;
            }

            void erase(Entries::iterator item) noexcept
            {
                residentBytes_ -= item->data->size();
                entries_.erase(item);
            }

            mutable std::mutex mutex_;
            Entries entries_; // Most recently accessed/released at the front.
            std::size_t residentBytes_{};
            std::size_t reservedBytes_{};
            std::uint64_t lastIdentity_{};
        };

        RawDataLease::~RawDataLease()
        {
            data.reset();
            if (pool) pool->released(identity);
        }

        const std::shared_ptr<RawDataPool>& rawDataPool()
        {
            // Leases retain the pool through static shutdown as well. The
            // pool never holds a strong lease, so this does not form a cycle.
            static const auto pool = std::make_shared<RawDataPool>();
            return pool;
        }

        class RawDataReservation final
        {
        public:
            RawDataReservation(std::shared_ptr<RawDataPool> pool, std::size_t size)
                : pool_(std::move(pool)), size_(size) { pool_->reserve(size_); }
            ~RawDataReservation() { if (size_) pool_->cancelReservation(size_); }
            RawDataReservation(const RawDataReservation&) = delete;
            RawDataReservation& operator=(const RawDataReservation&) = delete;
            SharedDataBytes publish(SharedDataBytes data)
            {
                auto result = pool_->publish(std::move(data), size_);
                size_ = 0;
                return result;
            }
        private:
            std::shared_ptr<RawDataPool> pool_;
            std::size_t size_{};
        };

    }

    namespace
    {
        constexpr std::array<char, 7> ArtechSignature
        {{ 'A', 'r', 't', 'e', 'c', 'h', '\0' }};


        [[nodiscard]] std::uint16_t readU16Le(
            const std::byte* bytes) noexcept
        {
            return
                static_cast<std::uint16_t>(
                    std::to_integer<std::uint8_t>(bytes[0])) |
                static_cast<std::uint16_t>(
                    std::to_integer<std::uint8_t>(bytes[1]) << 8U);
        }


        [[nodiscard]] std::uint32_t readU32Le(
            const std::byte* bytes) noexcept
        {
            return
                static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[0])) |
                (static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[1])) << 8U) |
                (static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[2])) << 16U) |
                (static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[3])) << 24U);
        }


        [[nodiscard]] bool readExact(
            std::ifstream& stream,
            std::span<std::byte> destination)
        {
            if (destination.empty())
            {
                return true;
            }

            stream.read(
                reinterpret_cast<char*>(destination.data()),
                static_cast<std::streamsize>(destination.size()));

            return
                stream.gcount() ==
                static_cast<std::streamsize>(destination.size());
        }


        [[nodiscard]] std::string itemDetail(
            std::string_view prefix,
            std::size_t item)
        {
            std::ostringstream output;
            output << prefix << " (item " << item << ')';
            return output.str();
        }
    }

    RawDataCacheStats rawDataCacheStats() noexcept
    {
        return detail::rawDataPool()->stats();
    }

    std::string_view legacyDataTypeName(LegacyDataType type) noexcept
    {
        switch (type)
        {
        case LegacyDataType::Unknown: return "Unknown";
        case LegacyDataType::Bitmap: return "BMP";
        case LegacyDataType::DopeTable: return "DopeTAB";
        case LegacyDataType::Uap: return "UAP";
        case LegacyDataType::Native: return "Native";
        case LegacyDataType::GbmTexture: return "GBMTexture";
        case LegacyDataType::GbmPicture: return "GBMPicture";
        case LegacyDataType::GenericBitmap: return "GenericBitmap";
        case LegacyDataType::Wave: return "Wave";
        case LegacyDataType::String: return "String";
        case LegacyDataType::UserCreated1: return "UserCreated1";
        case LegacyDataType::IndexTable: return "IndexTable";
        case LegacyDataType::Chunky: return "Chunky";
        case LegacyDataType::TextureArray: return "TextureArray";
        case LegacyDataType::Model3D: return "3DModel";
        case LegacyDataType::Pose3D: return "3DPose";
        case LegacyDataType::Hmd: return "HMD";
        case LegacyDataType::MeshX: return "MESHX";
        }

        return "Invalid";
    }


    std::string_view dataErrorCodeName(DataErrorCode code) noexcept
    {
        switch (code)
        {
        case DataErrorCode::None: return "None";
        case DataErrorCode::InvalidGroup: return "InvalidGroup";
        case DataErrorCode::InvalidLanguage: return "InvalidLanguage";
        case DataErrorCode::NoActiveLanguage: return "NoActiveLanguage";
        case DataErrorCode::DuplicateGroup: return "DuplicateGroup";
        case DataErrorCode::GroupNotMounted: return "GroupNotMounted";
        case DataErrorCode::ArchiveClosed: return "ArchiveClosed";
        case DataErrorCode::FileOpenFailed: return "FileOpenFailed";
        case DataErrorCode::FileSizeFailed: return "FileSizeFailed";
        case DataErrorCode::HeaderTruncated: return "HeaderTruncated";
        case DataErrorCode::InvalidSignature: return "InvalidSignature";
        case DataErrorCode::UnsupportedVersion: return "UnsupportedVersion";
        case DataErrorCode::InvalidHeader: return "InvalidHeader";
        case DataErrorCode::IndexTooLarge: return "IndexTooLarge";
        case DataErrorCode::IndexTruncated: return "IndexTruncated";
        case DataErrorCode::InvalidItemType: return "InvalidItemType";
        case DataErrorCode::InvalidItemRange: return "InvalidItemRange";
        case DataErrorCode::ChecksumMismatch: return "ChecksumMismatch";
        case DataErrorCode::TagOutOfRange: return "TagOutOfRange";
        case DataErrorCode::EmptyItem: return "EmptyItem";
        case DataErrorCode::ReadFailed: return "ReadFailed";
        case DataErrorCode::DecompressionFailed: return "DecompressionFailed";
        case DataErrorCode::DecompressedSizeMismatch:
            return "DecompressedSizeMismatch";
        case DataErrorCode::ArchiveBuildFailed: return "ArchiveBuildFailed";
        case DataErrorCode::WriteFailed: return "WriteFailed";
        case DataErrorCode::InvalidIndexTable: return "InvalidIndexTable";
        case DataErrorCode::UnsortedIndexTable:
            return "UnsortedIndexTable";
        case DataErrorCode::DuplicateIndexKey: return "DuplicateIndexKey";
        case DataErrorCode::IndexedItemNotFound:
            return "IndexedItemNotFound";
        case DataErrorCode::TypeMismatch: return "TypeMismatch";
        case DataErrorCode::InvalidUtf16: return "InvalidUtf16";
        case DataErrorCode::InvalidBoardEdition: return "InvalidBoardEdition";
        case DataErrorCode::ResourcePathInvalid: return "ResourcePathInvalid";
        case DataErrorCode::ResourceNotFound: return "ResourceNotFound";
        case DataErrorCode::ResourcePathAmbiguous: return "ResourcePathAmbiguous";
        case DataErrorCode::ResourcePathFailed: return "ResourcePathFailed";
        }

        return "InvalidDataErrorCode";
    }


    DataError LegacyDataArchive::makeError(
        DataErrorCode code,
        const std::filesystem::path& path,
        std::string detail,
        std::optional<DataTag> tag)
    {
        return { code, path, tag, std::move(detail) };
    }


    LegacyDataArchive::~LegacyDataArchive()
    {
        close();
    }


    std::expected<std::shared_ptr<LegacyDataArchive>, DataError>
    LegacyDataArchive::open(
        const std::filesystem::path& path,
        std::uint16_t group,
        ArchiveOpenOptions options)
    {
        if (group == 0)
        {
            return std::unexpected(makeError(
                DataErrorCode::InvalidGroup,
                path,
                "legacy group zero is reserved for EmptyDataId"));
        }

        auto archive = std::shared_ptr<LegacyDataArchive>(
            new LegacyDataArchive());
        archive->rawDataPool_ = detail::rawDataPool();

        archive->path_ = path;
        archive->group_ = group;
        archive->maximumUncompressedItemSize_ =
            options.maximumUncompressedItemSize;
        archive->stream_.open(path, std::ios::binary);

        if (!archive->stream_)
        {
            return std::unexpected(makeError(
                DataErrorCode::FileOpenFailed,
                path,
                "unable to open the legacy DAT bank"));
        }

        std::error_code sizeError;
        archive->fileSize_ = std::filesystem::file_size(path, sizeError);

        if (sizeError)
        {
            return std::unexpected(makeError(
                DataErrorCode::FileSizeFailed,
                path,
                sizeError.message()));
        }

        std::array<std::byte, PhysicalHeaderSize> header{};

        if (!readExact(archive->stream_, header))
        {
            return std::unexpected(makeError(
                DataErrorCode::HeaderTruncated,
                path,
                "DAT header must contain exactly 28 bytes"));
        }

        for (std::size_t index = 0; index < ArtechSignature.size(); ++index)
        {
            if (std::to_integer<char>(header[index]) !=
                ArtechSignature[index])
            {
                return std::unexpected(makeError(
                    DataErrorCode::InvalidSignature,
                    path,
                    "missing Artech\\0 signature"));
            }
        }

        archive->version_ =
        {
            readU16Le(header.data() + 8),
            readU16Le(header.data() + 10),
            readU16Le(header.data() + 12),
            readU16Le(header.data() + 14)
        };

        if (archive->version_.major != SupportedVersionMajor ||
            archive->version_.minor != SupportedVersionMinor)
        {
            return std::unexpected(makeError(
                DataErrorCode::UnsupportedVersion,
                path,
                "only the source-defined DAT version 2.3 is supported"));
        }

        const auto rawItemCount = readU32Le(header.data() + 20);

        // Les deux champs reserves sont volontairement ignores : L_Data les
        // lisait mais ne leur imposait aucune valeur. En revanche, son groupe
        // d'items exigeait au moins une entree et un DataTag reste sur 16 bits.
        if (rawItemCount == 0)
        {
            return std::unexpected(makeError(
                DataErrorCode::InvalidHeader,
                path,
                "DAT index must contain at least one item"));
        }

        if (rawItemCount >
            static_cast<std::uint32_t>(MaximumItemCount))
        {
            return std::unexpected(makeError(
                DataErrorCode::IndexTooLarge,
                path,
                "DAT index cannot exceed the 16-bit DataTag space"));
        }

        archive->checksum_ = readU32Le(header.data() + 24);

        const std::uint64_t indexBytes =
            static_cast<std::uint64_t>(rawItemCount) *
            PhysicalIndexRecordSize;
        const std::uint64_t dataStart = PhysicalHeaderSize + indexBytes;

        if (dataStart > archive->fileSize_)
        {
            return std::unexpected(makeError(
                DataErrorCode::IndexTruncated,
                path,
                "DAT index extends past end of file"));
        }

        archive->items_.reserve(rawItemCount);

        std::array<std::byte, PhysicalIndexRecordSize> record{};

        for (std::uint32_t item = 0; item < rawItemCount; ++item)
        {
            if (!readExact(archive->stream_, record))
            {
                return std::unexpected(makeError(
                    DataErrorCode::IndexTruncated,
                    path,
                    itemDetail("unable to read complete DAT index record", item),
                    static_cast<DataTag>(item)));
            }

            const auto offset = readU32Le(record.data());
            const auto uncompressedSize = readU32Le(record.data() + 4);
            const auto rawType = readU32Le(record.data() + 8);
            const auto compressedSize = readU32Le(record.data() + 12);

            if (rawType > static_cast<std::uint32_t>(LegacyDataType::MeshX))
            {
                return std::unexpected(makeError(
                    DataErrorCode::InvalidItemType,
                    path,
                    itemDetail("data type is outside LE_DATA_DataType", item),
                    static_cast<DataTag>(item)));
            }

            ArchiveItemMetadata metadata
            {
                static_cast<DataTag>(item),
                static_cast<LegacyDataType>(rawType),
                offset,
                uncompressedSize,
                compressedSize
            };

            if (!metadata.present())
            {
                archive->items_.push_back(metadata);
                continue;
            }

            const bool invalidSignedField =
                offset > static_cast<std::uint32_t>(
                    std::numeric_limits<std::int32_t>::max()) ||
                uncompressedSize > static_cast<std::uint32_t>(
                    std::numeric_limits<std::int32_t>::max()) ||
                compressedSize > static_cast<std::uint32_t>(
                    std::numeric_limits<std::int32_t>::max());

            const std::uint64_t itemEnd =
                static_cast<std::uint64_t>(offset) + compressedSize;

            if (invalidSignedField ||
                metadata.type == LegacyDataType::Unknown ||
                uncompressedSize == 0 ||
                compressedSize == 0 ||
                uncompressedSize > options.maximumUncompressedItemSize ||
                offset < dataStart ||
                itemEnd > archive->fileSize_)
            {
                return std::unexpected(makeError(
                    DataErrorCode::InvalidItemRange,
                    path,
                    itemDetail(
                        "DAT item offset or size is outside validated bounds",
                        item),
                    static_cast<DataTag>(item)));
            }

            archive->items_.push_back(metadata);
        }

        if (options.checksumPolicy == ChecksumPolicy::Verify)
        {
            archive->stream_.clear();
            archive->stream_.seekg(
                static_cast<std::streamoff>(PhysicalHeaderSize),
                std::ios::beg);

            uLong checksum = crc32(0L, Z_NULL, 0);
            std::array<std::byte, 64U * 1024U> block{};
            std::uint64_t bytesRemaining =
                archive->fileSize_ - PhysicalHeaderSize;

            while (bytesRemaining > 0)
            {
                const auto amount = static_cast<std::size_t>(std::min<
                    std::uint64_t>(bytesRemaining, block.size()));
                auto destination = std::span<std::byte>(block).first(amount);

                if (!readExact(archive->stream_, destination))
                {
                    return std::unexpected(makeError(
                        DataErrorCode::ReadFailed,
                        path,
                        "unable to read DAT bytes for CRC-32 validation"));
                }

                checksum = crc32(
                    checksum,
                    reinterpret_cast<const Bytef*>(block.data()),
                    static_cast<uInt>(amount));
                bytesRemaining -= amount;
            }

            if (static_cast<std::uint32_t>(checksum) != archive->checksum_)
            {
                return std::unexpected(makeError(
                    DataErrorCode::ChecksumMismatch,
                    path,
                    "stored CRC-32 does not match bytes after the header"));
            }
        }

        archive->stream_.clear();
        archive->cache_.resize(archive->items_.size());
        archive->open_ = true;
        return archive;
    }


    void LegacyDataArchive::close() noexcept
    {
        std::scoped_lock lock(mutex_);
        for (auto& item : cache_) rawDataPool_->remove(item);
        cache_.clear();

        if (stream_.is_open())
        {
            stream_.close();
        }

        open_ = false;
    }


    bool LegacyDataArchive::isOpen() const noexcept
    {
        std::scoped_lock lock(mutex_);
        return open_;
    }


    std::uint16_t LegacyDataArchive::group() const noexcept
    {
        return group_;
    }


    const std::filesystem::path& LegacyDataArchive::path() const noexcept
    {
        return path_;
    }


    ArchiveVersion LegacyDataArchive::version() const noexcept
    {
        return version_;
    }


    std::uint32_t LegacyDataArchive::storedChecksum() const noexcept
    {
        return checksum_;
    }


    std::size_t LegacyDataArchive::itemCount() const noexcept
    {
        return items_.size();
    }


    std::expected<ArchiveItemMetadata, DataError>
    LegacyDataArchive::metadata(DataTag tag) const
    {
        if (static_cast<std::size_t>(tag) >= items_.size())
        {
            return std::unexpected(makeError(
                DataErrorCode::TagOutOfRange,
                path_,
                "DataTag is outside this archive index",
                tag));
        }

        return items_[tag];
    }


    std::expected<SharedDataBytes, DataError>
    LegacyDataArchive::load(DataTag tag)
    {
        std::scoped_lock lock(mutex_);

        if (!open_)
        {
            return std::unexpected(makeError(
                DataErrorCode::ArchiveClosed,
                path_,
                "cannot load an item from a closed archive",
                tag));
        }

        if (static_cast<std::size_t>(tag) >= items_.size())
        {
            return std::unexpected(makeError(
                DataErrorCode::TagOutOfRange,
                path_,
                "DataTag is outside this archive index",
                tag));
        }

        if (auto cached = rawDataPool_->acquire(cache_[tag]))
        {
            return cached;
        }

        const auto& item = items_[tag];

        if (!item.present())
        {
            return std::unexpected(makeError(
                DataErrorCode::EmptyItem,
                path_,
                "DataTag identifies an empty DAT slot",
                tag));
        }

        detail::RawDataReservation reservation(rawDataPool_, item.uncompressedSize);
        auto compressed = std::vector<Bytef>(item.compressedSize);
        stream_.clear();
        stream_.seekg(
            static_cast<std::streamoff>(item.offset),
            std::ios::beg);

        if (!stream_ ||
            !readExact(
                stream_,
                std::as_writable_bytes(std::span(compressed))))
        {
            return std::unexpected(makeError(
                DataErrorCode::ReadFailed,
                path_,
                "unable to read the complete compressed DAT item",
                tag));
        }

        auto output = std::make_shared<DataBytes>(item.uncompressedSize);
        uLongf outputSize = item.uncompressedSize;
        uLong compressedBytesConsumed = item.compressedSize;

        const int result = uncompress2(
            reinterpret_cast<Bytef*>(output->data()),
            &outputSize,
            compressed.data(),
            &compressedBytesConsumed);

        if (result != Z_OK)
        {
            std::ostringstream detail;
            detail
                << "zlib inflate failed with code "
                << result;

            return std::unexpected(makeError(
                DataErrorCode::DecompressionFailed,
                path_,
                detail.str(),
                tag));
        }

        if (outputSize != item.uncompressedSize ||
            compressedBytesConsumed != item.compressedSize)
        {
            return std::unexpected(makeError(
                DataErrorCode::DecompressedSizeMismatch,
                path_,
                "inflated size or consumed compressed size differs from index",
                tag));
        }

        cache_[tag] = output;
        return reservation.publish(std::move(output));
    }


    std::expected<std::size_t, DataError> LegacyDataArchive::readRaw(
        DataTag tag,
        std::span<std::byte> destination,
        std::uint32_t startOffset)
    {
        std::scoped_lock lock(mutex_);

        if (!open_)
        {
            return std::unexpected(makeError(
                DataErrorCode::ArchiveClosed,
                path_,
                "cannot read raw bytes from a closed archive",
                tag));
        }

        if (static_cast<std::size_t>(tag) >= items_.size())
        {
            return std::unexpected(makeError(
                DataErrorCode::TagOutOfRange,
                path_,
                "DataTag is outside this archive index",
                tag));
        }

        const auto& item = items_[tag];
        if (!item.present())
        {
            return std::unexpected(makeError(
                DataErrorCode::EmptyItem,
                path_,
                "DataTag identifies an empty DAT slot",
                tag));
        }

        if (destination.empty() || startOffset >= item.uncompressedSize)
            return std::size_t{0};

        const auto amount = std::min<std::size_t>(
            destination.size(),
            static_cast<std::size_t>(item.uncompressedSize - startOffset));

        std::vector<Bytef> compressed(item.compressedSize);
        stream_.clear();
        stream_.seekg(
            static_cast<std::streamoff>(item.offset),
            std::ios::beg);

        if (!stream_ ||
            !readExact(
                stream_,
                std::as_writable_bytes(std::span(compressed))))
        {
            return std::unexpected(makeError(
                DataErrorCode::ReadFailed,
                path_,
                "unable to read the complete compressed DAT item",
                tag));
        }

        z_stream inflater{};
        inflater.next_in = compressed.data();
        inflater.avail_in = static_cast<uInt>(compressed.size());
        if (inflateInit(&inflater) != Z_OK)
        {
            return std::unexpected(makeError(
                DataErrorCode::DecompressionFailed,
                path_,
                "zlib inflate initialization failed",
                tag));
        }

        struct InflateGuard
        {
            z_stream* stream{};
            ~InflateGuard()
            {
                if (stream) inflateEnd(stream);
            }
        } guard{&inflater};

        std::array<std::byte, 64U * 1024U> block{};
        std::uint64_t producedTotal{};
        std::size_t copied{};
        const std::uint64_t wantedStart = startOffset;
        const std::uint64_t wantedEnd =
            wantedStart + static_cast<std::uint64_t>(amount);

        while (copied < amount)
        {
            inflater.next_out =
                reinterpret_cast<Bytef*>(block.data());
            inflater.avail_out = static_cast<uInt>(block.size());

            const int result = inflate(&inflater, Z_NO_FLUSH);
            if (result != Z_OK && result != Z_STREAM_END)
            {
                return std::unexpected(makeError(
                    DataErrorCode::DecompressionFailed,
                    path_,
                    "zlib inflate failed during raw slice read",
                    tag));
            }

            const auto produced = block.size() - inflater.avail_out;
            const std::uint64_t blockStart = producedTotal;
            const std::uint64_t blockEnd =
                producedTotal + static_cast<std::uint64_t>(produced);

            if (blockEnd > wantedStart && blockStart < wantedEnd)
            {
                const auto overlapStart =
                    std::max(blockStart, wantedStart);
                const auto overlapEnd =
                    std::min(blockEnd, wantedEnd);
                const auto sourceOffset =
                    static_cast<std::size_t>(overlapStart - blockStart);
                const auto copyCount =
                    static_cast<std::size_t>(overlapEnd - overlapStart);
                std::memcpy(
                    destination.data() + copied,
                    block.data() + sourceOffset,
                    copyCount);
                copied += copyCount;
            }

            producedTotal = blockEnd;

            if (result == Z_STREAM_END)
                break;

            if (produced == 0 && inflater.avail_in == 0)
            {
                return std::unexpected(makeError(
                    DataErrorCode::DecompressedSizeMismatch,
                    path_,
                    "compressed DAT item ended before the requested raw slice",
                    tag));
            }
        }

        if (copied != amount)
        {
            return std::unexpected(makeError(
                DataErrorCode::DecompressedSizeMismatch,
                path_,
                "decompressed DAT item ended before the requested raw slice",
                tag));
        }

        return copied;
    }


    std::expected<bool, DataError> LegacyDataArchive::isLoaded(DataTag tag) const
    {
        std::scoped_lock lock(mutex_);

        if (!open_)
        {
            return std::unexpected(makeError(
                DataErrorCode::ArchiveClosed,
                path_,
                "cannot query loaded state from a closed archive",
                tag));
        }

        if (static_cast<std::size_t>(tag) >= items_.size())
        {
            return std::unexpected(makeError(
                DataErrorCode::TagOutOfRange,
                path_,
                "DataTag is outside this archive index",
                tag));
        }

        if (!items_[tag].present())
            return false;

        return !cache_[tag].expired();
    }


    std::expected<bool, DataError> LegacyDataArchive::unload(DataTag tag)
    {
        std::scoped_lock lock(mutex_);

        if (!open_)
        {
            return std::unexpected(makeError(
                DataErrorCode::ArchiveClosed,
                path_,
                "cannot unload an item from a closed archive",
                tag));
        }

        if (static_cast<std::size_t>(tag) >= items_.size())
        {
            return std::unexpected(makeError(
                DataErrorCode::TagOutOfRange,
                path_,
                "DataTag is outside this archive index",
                tag));
        }

        const bool wasCached = rawDataPool_->remove(cache_[tag]);
        cache_[tag].reset();
        return wasCached;
    }


    void LegacyDataArchive::clearCache() noexcept
    {
        std::scoped_lock lock(mutex_);

        for (auto& item : cache_)
        {
            rawDataPool_->remove(item);
            item.reset();
        }
    }


    std::size_t LegacyDataArchive::cachedItemCount() const noexcept
    {
        std::scoped_lock lock(mutex_);
        return static_cast<std::size_t>(std::count_if(
            cache_.begin(),
            cache_.end(),
            [](const std::weak_ptr<const DataBytes>& item)
            {
                return !item.expired();
            }));
    }


    std::expected<std::shared_ptr<LegacyDataArchive>, DataError>
    DataBankRegistry::mount(
        const std::filesystem::path& path,
        std::uint16_t group,
        ArchiveOpenOptions options)
    {
        {
            std::scoped_lock lock(mutex_);

            if (archives_.contains(group))
            {
                return std::unexpected(DataError
                {
                    DataErrorCode::DuplicateGroup,
                    path,
                    std::nullopt,
                    "a DAT archive is already mounted for this group"
                });
            }
        }

        auto opened = LegacyDataArchive::open(path, group, options);

        if (!opened)
        {
            return std::unexpected(opened.error());
        }

        std::scoped_lock lock(mutex_);

        const auto [iterator, inserted] = archives_.emplace(group, *opened);

        if (!inserted)
        {
            (*opened)->close();
            return std::unexpected(DataError
            {
                DataErrorCode::DuplicateGroup,
                path,
                std::nullopt,
                "a DAT archive was mounted concurrently for this group"
            });
        }

        return iterator->second;
    }


    std::expected<std::shared_ptr<LegacyDataArchive>, DataError>
    DataBankRegistry::archive(std::uint16_t group) const
    {
        std::scoped_lock lock(mutex_);
        const auto found = archives_.find(group);

        if (found == archives_.end())
        {
            return std::unexpected(DataError
            {
                DataErrorCode::GroupNotMounted,
                {},
                std::nullopt,
                "no DAT archive is mounted for this group"
            });
        }

        return found->second;
    }


    std::expected<ArchiveItemMetadata, DataError>
    DataBankRegistry::metadata(DataId id) const
    {
        auto mounted = archive(dataGroup(id));

        if (!mounted)
        {
            return std::unexpected(mounted.error());
        }

        return (*mounted)->metadata(dataTag(id));
    }


    std::expected<SharedDataBytes, DataError>
    DataBankRegistry::load(DataId id) const
    {
        auto mounted = archive(dataGroup(id));

        if (!mounted)
        {
            return std::unexpected(mounted.error());
        }

        return (*mounted)->load(dataTag(id));
    }


    std::expected<std::size_t, DataError>
    DataBankRegistry::readRaw(
        DataId id,
        std::span<std::byte> destination,
        std::uint32_t startOffset) const
    {
        if (isEmptyDataId(id))
            return std::size_t{0};

        auto mounted = archive(dataGroup(id));
        if (!mounted)
            return std::unexpected(mounted.error());

        return (*mounted)->readRaw(
            dataTag(id), destination, startOffset);
    }


    std::expected<bool, DataError>
    DataBankRegistry::isLoaded(DataId id) const
    {
        if (isEmptyDataId(id))
            return false;

        auto mounted = archive(dataGroup(id));
        if (!mounted)
            return std::unexpected(mounted.error());

        return (*mounted)->isLoaded(dataTag(id));
    }


    std::expected<void, DataError>
    DataBankRegistry::unload(DataId id) const
    {
        if (isEmptyDataId(id))
            return {};

        auto mounted = archive(dataGroup(id));
        if (!mounted)
            return std::unexpected(mounted.error());

        const auto removed = (*mounted)->unload(dataTag(id));
        if (!removed)
            return std::unexpected(removed.error());

        // LE_DATA_Unload succeeds when the item is already out of memory.
        // The bool returned by LegacyDataArchive::unload only reports whether
        // cache ownership was actually removed during this call.
        return {};
    }


    void DataBankRegistry::clearCaches() noexcept
    {
        std::vector<std::shared_ptr<LegacyDataArchive>> archives;
        {
            std::scoped_lock lock(mutex_);
            archives.reserve(archives_.size());
            for (const auto& [group, archive] : archives_)
            {
                static_cast<void>(group);
                archives.push_back(archive);
            }
        }

        for (const auto& archive : archives)
            archive->clearCache();
    }


    std::size_t DataBankRegistry::cachedItemCount() const noexcept
    {
        std::vector<std::shared_ptr<LegacyDataArchive>> archives;
        {
            std::scoped_lock lock(mutex_);
            archives.reserve(archives_.size());
            for (const auto& [group, archive] : archives_)
            {
                static_cast<void>(group);
                archives.push_back(archive);
            }
        }

        std::size_t total{};
        for (const auto& archive : archives)
            total += archive->cachedItemCount();
        return total;
    }


    std::expected<void, DataError>
    DataBankRegistry::unmount(std::uint16_t group)
    {
        std::shared_ptr<LegacyDataArchive> removed;

        {
            std::scoped_lock lock(mutex_);
            const auto found = archives_.find(group);

            if (found == archives_.end())
            {
                return std::unexpected(DataError
                {
                    DataErrorCode::GroupNotMounted,
                    {},
                    std::nullopt,
                    "no DAT archive is mounted for this group"
                });
            }

            removed = std::move(found->second);
            archives_.erase(found);
        }

        removed->close();
        return {};
    }


    void DataBankRegistry::clear() noexcept
    {
        std::unordered_map<
            std::uint16_t,
            std::shared_ptr<LegacyDataArchive>> removed;

        {
            std::scoped_lock lock(mutex_);
            removed.swap(archives_);
        }

        for (auto& [group, archive] : removed)
        {
            static_cast<void>(group);
            archive->close();
        }
    }


    std::size_t DataBankRegistry::mountedCount() const noexcept
    {
        std::scoped_lock lock(mutex_);
        return archives_.size();
    }


    std::expected<DataIndexTable, DataError>
    DataIndexTable::parse(std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() % PhysicalEntrySize != 0)
        {
            return std::unexpected(DataError
            {
                DataErrorCode::InvalidIndexTable,
                {},
                std::nullopt,
                "index table size must be a non-zero multiple of 6 bytes"
            });
        }

        DataIndexTable table;
        table.entries_.reserve(bytes.size() / PhysicalEntrySize);

        for (std::size_t offset = 0;
            offset < bytes.size();
            offset += PhysicalEntrySize)
        {
            DataIndexEntry entry
            {
                readU32Le(bytes.data() + offset),
                readU16Le(bytes.data() + offset + 4)
            };

            if (!table.entries_.empty())
            {
                const auto previous = table.entries_.back().indexValue;

                if (entry.indexValue < previous)
                {
                    return std::unexpected(DataError
                    {
                        DataErrorCode::UnsortedIndexTable,
                        {},
                        std::nullopt,
                        "index entries must be sorted for legacy bsearch"
                    });
                }

                if (entry.indexValue == previous)
                {
                    return std::unexpected(DataError
                    {
                        DataErrorCode::DuplicateIndexKey,
                        {},
                        std::nullopt,
                        "duplicate keys make legacy bsearch nondeterministic"
                    });
                }
            }

            table.entries_.push_back(entry);
        }

        return table;
    }


    std::optional<DataTag> DataIndexTable::find(
        std::uint32_t indexValue) const noexcept
    {
        const auto found = std::lower_bound(
            entries_.begin(),
            entries_.end(),
            indexValue,
            [](const DataIndexEntry& entry, std::uint32_t key)
            {
                return entry.indexValue < key;
            });

        if (found == entries_.end() || found->indexValue != indexValue)
        {
            return std::nullopt;
        }

        return found->dataTag;
    }


    std::span<const DataIndexEntry> DataIndexTable::entries() const noexcept
    {
        return entries_;
    }


    std::expected<DataId, DataError> lookupIndexedDataId(
        const DataBankRegistry& registry,
        DataId indexTableId,
        std::uint32_t indexValue)
    {
        auto metadata = registry.metadata(indexTableId);

        if (!metadata)
        {
            return std::unexpected(metadata.error());
        }

        if (metadata->type != LegacyDataType::IndexTable)
        {
            return std::unexpected(DataError
            {
                DataErrorCode::TypeMismatch,
                {},
                dataTag(indexTableId),
                "indexed lookup requires a LE_DATA_DataIndexTable item"
            });
        }

        auto bytes = registry.load(indexTableId);

        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }

        auto table = DataIndexTable::parse(**bytes);

        if (!table)
        {
            return std::unexpected(table.error());
        }

        const auto tag = table->find(indexValue);

        if (!tag)
        {
            return std::unexpected(DataError
            {
                DataErrorCode::IndexedItemNotFound,
                {},
                std::nullopt,
                "requested key does not exist in the index table"
            });
        }

        return packDataId(dataGroup(indexTableId), *tag);
    }

    std::expected<DataId, DataError> lookupIndexedDataIdLegacy(
        const DataBankRegistry& registry,
        DataId indexTableId,
        std::uint32_t indexValue)
    {
        const auto result = lookupIndexedDataId(
            registry, indexTableId, indexValue);
        if (result) return *result;

        if (result.error().code == DataErrorCode::TypeMismatch ||
            result.error().code == DataErrorCode::IndexedItemNotFound)
            return EmptyDataId;

        return std::unexpected(result.error());
    }
}
