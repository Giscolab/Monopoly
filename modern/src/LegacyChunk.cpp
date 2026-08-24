#include "LegacyChunk.hpp"

#include <algorithm>

namespace monopoly::data
{
    namespace
    {
        [[nodiscard]] std::uint32_t readChunkSize(
            const std::byte* bytes) noexcept
        {
            // Representation Win32 prouvee du bitfield source : les 24 bits
            // bas portent la taille totale, le dernier octet porte l'ID.
            return
                static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[0])) |
                (static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[1])) << 8U) |
                (static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[2])) << 16U);
        }
    }


    std::string_view chunkErrorCodeName(ChunkErrorCode code) noexcept
    {
        switch (code)
        {
        case ChunkErrorCode::None: return "None";
        case ChunkErrorCode::EndOfParent: return "EndOfParent";
        case ChunkErrorCode::ChunkNotFound: return "ChunkNotFound";
        case ChunkErrorCode::HeaderTruncated: return "HeaderTruncated";
        case ChunkErrorCode::InvalidSize: return "InvalidSize";
        case ChunkErrorCode::ChunkPastParent: return "ChunkPastParent";
        case ChunkErrorCode::InvalidId: return "InvalidId";
        case ChunkErrorCode::MaximumDepth: return "MaximumDepth";
        case ChunkErrorCode::AlreadyAtRoot: return "AlreadyAtRoot";
        case ChunkErrorCode::InvalidLevel: return "InvalidLevel";
        case ChunkErrorCode::PositionOutOfRange:
            return "PositionOutOfRange";
        }

        return "InvalidChunkErrorCode";
    }


    LegacyChunkReader::LegacyChunkReader(
        std::span<const std::byte> bytes) noexcept
        : bytes_(bytes)
    {
        levels_[0] = { 0, bytes_.size(), NullStandardId };
    }


    LegacyChunkReader::LegacyChunkReader(SharedDataBytes bytes) noexcept
        : owner_(std::move(bytes)),
          bytes_(owner_ ? std::span<const std::byte>(*owner_) :
              std::span<const std::byte>{})
    {
        levels_[0] = { 0, bytes_.size(), NullStandardId };
    }


    ChunkError LegacyChunkReader::error(
        ChunkErrorCode code,
        std::string detail,
        std::size_t offset) const
    {
        return { code, offset, std::move(detail) };
    }


    std::expected<ChunkInfo, ChunkError> LegacyChunkReader::descend(
        std::uint8_t findId)
    {
        if (level_ + 1 >= MaximumLevels)
        {
            return std::unexpected(error(
                ChunkErrorCode::MaximumDepth,
                "legacy chunk nesting cannot exceed seven child levels",
                currentOffset_));
        }

        const std::size_t parentEnd = levels_[level_].end;
        std::size_t candidateOffset = currentOffset_;

        while (candidateOffset < parentEnd)
        {
            const std::size_t bytesRemaining = parentEnd - candidateOffset;

            if (bytesRemaining < PhysicalHeaderSize)
            {
                return std::unexpected(error(
                    ChunkErrorCode::HeaderTruncated,
                    "fewer than four bytes remain for a chunk header",
                    candidateOffset));
            }

            const auto* header = bytes_.data() + candidateOffset;
            const std::uint32_t totalSize = readChunkSize(header);
            const std::uint8_t id =
                std::to_integer<std::uint8_t>(header[3]);

            if (totalSize < PhysicalHeaderSize)
            {
                return std::unexpected(error(
                    ChunkErrorCode::InvalidSize,
                    "chunk size includes its header and cannot be below four",
                    candidateOffset));
            }

            if (static_cast<std::size_t>(totalSize) > bytesRemaining)
            {
                return std::unexpected(error(
                    ChunkErrorCode::ChunkPastParent,
                    "chunk extends beyond its parent boundary",
                    candidateOffset));
            }

            const std::size_t chunkEnd = candidateOffset + totalSize;

            if (findId == NullStandardId || findId == id)
            {
                // LE_CHUNK_Descend ne testait le sentinel nul qu'apres avoir
                // trouve le chunk demande. Une recherche par ID peut donc
                // franchir un sibling 0/128 bien forme; descend() (next) ou
                // une recherche explicite de 128 doivent en revanche echouer.
                if ((id & 0x7FU) == 0)
                {
                    return std::unexpected(error(
                        ChunkErrorCode::InvalidId,
                        "selected chunk ID 0 or 128 is a null sentinel",
                        candidateOffset));
                }

                ++level_;
                const std::size_t dataStart =
                    candidateOffset + PhysicalHeaderSize;
                levels_[level_] = { dataStart, chunkEnd, id };
                currentOffset_ = dataStart;

                return ChunkInfo
                {
                    id,
                    candidateOffset,
                    dataStart,
                    static_cast<std::size_t>(totalSize) -
                        PhysicalHeaderSize,
                    chunkEnd
                };
            }

            candidateOffset = chunkEnd;
            currentOffset_ = candidateOffset;
        }

        return std::unexpected(error(
            findId == NullStandardId ?
                ChunkErrorCode::EndOfParent :
                ChunkErrorCode::ChunkNotFound,
            findId == NullStandardId ?
                "no next child chunk remains" :
                "requested chunk ID was not found among remaining siblings",
            currentOffset_));
    }


    std::expected<void, ChunkError> LegacyChunkReader::ascend()
    {
        if (level_ == 0)
        {
            return std::unexpected(error(
                ChunkErrorCode::AlreadyAtRoot,
                "cannot ascend above the root byte container",
                currentOffset_));
        }

        currentOffset_ = levels_[level_].end;
        --level_;
        return {};
    }


    std::expected<std::span<const std::byte>, ChunkError>
    LegacyChunkReader::map(std::size_t requestedBytes)
    {
        const std::size_t amount = std::min(requestedBytes, remaining());
        const auto mapped = bytes_.subspan(currentOffset_, amount);
        currentOffset_ += amount;
        return mapped;
    }


    std::expected<std::size_t, ChunkError> LegacyChunkReader::read(
        std::span<std::byte> destination)
    {
        auto mapped = map(destination.size());

        if (!mapped)
        {
            return std::unexpected(mapped.error());
        }

        std::copy(mapped->begin(), mapped->end(), destination.begin());
        return mapped->size();
    }


    std::expected<void, ChunkError> LegacyChunkReader::seek(
        std::size_t absoluteOffset)
    {
        const auto& currentLevel = levels_[level_];

        if (absoluteOffset < currentLevel.dataStart ||
            absoluteOffset > currentLevel.end)
        {
            return std::unexpected(error(
                ChunkErrorCode::PositionOutOfRange,
                "position must remain inside current chunk data",
                absoluteOffset));
        }

        currentOffset_ = absoluteOffset;
        return {};
    }


    std::size_t LegacyChunkReader::currentOffset() const noexcept
    {
        return currentOffset_;
    }


    std::size_t LegacyChunkReader::level() const noexcept
    {
        return level_;
    }


    std::uint8_t LegacyChunkReader::idForLevel(
        std::size_t level) const noexcept
    {
        return level <= level_ ? levels_[level].id : NullStandardId;
    }


    std::size_t LegacyChunkReader::dataSizeForLevel(
        std::size_t level) const noexcept
    {
        return level <= level_ ?
            levels_[level].end - levels_[level].dataStart : 0;
    }


    std::size_t LegacyChunkReader::dataStartForLevel(
        std::size_t level) const noexcept
    {
        return level <= level_ ? levels_[level].dataStart : 0;
    }


    std::size_t LegacyChunkReader::remaining() const noexcept
    {
        return levels_[level_].end - currentOffset_;
    }


    std::expected<LegacyChunkReader, DataError> openLegacyChunkReader(
        const DataBankRegistry& registry,
        DataId id)
    {
        auto metadata = registry.metadata(id);

        if (!metadata)
        {
            return std::unexpected(metadata.error());
        }

        if (metadata->type != LegacyDataType::Chunky)
        {
            return std::unexpected(DataError
            {
                DataErrorCode::TypeMismatch,
                {},
                dataTag(id),
                "LE_CHUNK_ReadFromDataID requires a DataChunky item"
            });
        }

        auto bytes = registry.load(id);

        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }

        return LegacyChunkReader(std::move(*bytes));
    }
}
