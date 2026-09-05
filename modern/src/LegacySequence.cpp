#include "LegacySequence.hpp"

#include <utility>

namespace monopoly::data
{
    namespace
    {
        std::uint32_t readU32(std::span<const std::byte> bytes,
            std::size_t offset) noexcept
        {
            return
                std::to_integer<std::uint32_t>(bytes[offset]) |
                (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
                (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
                (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
        }

        std::int32_t signed24(std::uint32_t word) noexcept
        {
            const auto value = static_cast<std::int32_t>(word & 0x00FF'FFFFU);
            return (word & 0x0080'0000U) != 0 ? value - 0x0100'0000 : value;
        }

        // L_Seqncr.h:574-677 et L_Chunk.cpp:551-565. Ce sont des tailles
        // de payload, sans les quatre octets de LE_CHUNK_HeaderRecord.
        std::size_t fixedRecordSize(std::uint8_t chunkId) noexcept
        {
            switch (chunkId)
            {
            case 1: return 12; // Grouping
            case 2: return 16; // Indirect
            case 3: return 16; // Bitmap
            case 4: return 24; // Model (geometry, texture table, joints)
            case 5: return 16; // Sound
            case 9: return 16; // Mesh
            default: return 0;
            }
        }
    }

    std::string_view sequenceErrorCodeName(SequenceErrorCode code) noexcept
    {
        switch (code)
        {
        case SequenceErrorCode::None: return "None";
        case SequenceErrorCode::HeaderTruncated: return "HeaderTruncated";
        case SequenceErrorCode::ChunkFailure: return "ChunkFailure";
        case SequenceErrorCode::UnsupportedRecord: return "UnsupportedRecord";
        case SequenceErrorCode::FixedRecordTruncated: return "FixedRecordTruncated";
        case SequenceErrorCode::RecordLimitExceeded: return "RecordLimitExceeded";
        case SequenceErrorCode::InvalidClockRange: return "InvalidClockRange";
        }
        return "InvalidSequenceErrorCode";
    }

    std::expected<LegacySequenceHeader, SequenceError>
    decodeLegacySequenceHeader(std::span<const std::byte> bytes)
    {
        if (bytes.size() < LegacySequenceHeaderSize)
        {
            return std::unexpected(SequenceError{
                SequenceErrorCode::HeaderTruncated, 0,
                "sequence common header requires three little-endian 32-bit words",
                std::nullopt });
        }

        // Win32 layout explicitly documented as twelve bytes in L_Seqncr.h.
        const auto first = readU32(bytes, 0);
        const auto second = readU32(bytes, 4);
        const auto third = readU32(bytes, 8);
        return LegacySequenceHeader{
            signed24(first),
            static_cast<std::uint8_t>(first >> 24U),
            signed24(second),
            static_cast<std::uint8_t>((second >> 24U) & 0x3FU),
            (second & 0x4000'0000U) != 0,
            (second & 0x8000'0000U) != 0,
            static_cast<std::uint8_t>(third & 0x07U),
            (third & 0x08U) != 0,
            (third & 0x10U) != 0,
            third >> 5U
        };
    }

    std::expected<LegacySequenceRecord, SequenceError>
    readLegacySequenceRecord(LegacyChunkReader& reader)
    {
        // Copier le curseur conserve egalement sa lease SharedDataBytes.
        // La publication n'a lieu qu'apres lecture de toute la partie fixe.
        auto candidate = reader;
        const auto chunk = candidate.descend();
        if (!chunk)
        {
            return std::unexpected(SequenceError{
                SequenceErrorCode::ChunkFailure, chunk.error().offset,
                "cannot enter the next sequence chunk", chunk.error() });
        }

        const auto fixedSize = fixedRecordSize(chunk->id);
        if (fixedSize == 0)
        {
            return std::unexpected(SequenceError{
                SequenceErrorCode::UnsupportedRecord, chunk->headerOffset,
                "sequence fixed record decoder does not support chunk ID " +
                    std::to_string(chunk->id), std::nullopt });
        }
        if (chunk->dataSize < fixedSize)
        {
            return std::unexpected(SequenceError{
                SequenceErrorCode::FixedRecordTruncated, chunk->dataOffset,
                "chunk payload is shorter than its sequence fixed record",
                std::nullopt });
        }

        const auto mapped = candidate.map(fixedSize);
        if (!mapped)
        {
            return std::unexpected(SequenceError{
                SequenceErrorCode::ChunkFailure, mapped.error().offset,
                "cannot map the sequence fixed record", mapped.error() });
        }
        const auto header = decodeLegacySequenceHeader(*mapped);
        if (!header)
        {
            auto error = header.error();
            error.offset += chunk->dataOffset;
            return std::unexpected(std::move(error));
        }

        LegacySequenceRecord record{ *chunk, *header,
            SequenceGroupingData{}, candidate.currentOffset() };
        switch (chunk->id)
        {
        case 1: break;
        case 2:
            record.data = SequenceIndirectData{ readU32(*mapped, 12) };
            break;
        case 3:
            record.data = SequenceBitmapData{ readU32(*mapped, 12) };
            break;
        case 4:
            record.data = SequenceModelData{ readU32(*mapped, 12),
                readU32(*mapped, 16), readU32(*mapped, 20) };
            break;
        case 5:
            record.data = SequenceSoundData{ readU32(*mapped, 12) };
            break;
        case 9:
            record.data = SequenceMeshData{ readU32(*mapped, 12) };
            break;
        }
        reader = std::move(candidate);
        return record;
    }
}
