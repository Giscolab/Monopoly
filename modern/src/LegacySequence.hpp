#pragma once

#include "LegacyChunk.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace monopoly::data
{
    enum class SequenceErrorCode
    {
        None,
        HeaderTruncated,
        ChunkFailure,
        UnsupportedRecord,
        FixedRecordTruncated,
        RecordLimitExceeded,
        InvalidClockRange
    };

    struct SequenceError
    {
        SequenceErrorCode code{ SequenceErrorCode::None };
        std::size_t offset{};
        std::string detail;
        std::optional<ChunkError> chunkError;
    };

    [[nodiscard]] std::string_view sequenceErrorCodeName(
        SequenceErrorCode code) noexcept;

    inline constexpr std::size_t LegacySequenceHeaderSize = 12;

    // Representation decodee de LE_SEQNCR_SequenceChunkHeaderRecord.
    // Ce contrat de lecture ne certifie pas qu'une sequence est executable :
    // endTime=0 (infini), timeMultiple=0, endingAction 0..7 et reserved sont
    // conserves tels quels. Le runtime source peut remplacer cadence/action.
    // Aucun bitfield C++ ni layout natif n'est utilise pour lire le disque.
    struct LegacySequenceHeader
    {
        std::int32_t parentStartTime{};
        std::uint8_t priority{};
        std::int32_t endTime{};
        std::uint8_t timeMultiple{};
        bool dropFrames{};
        bool lastUse{};
        std::uint8_t endingAction{};
        bool scrollingWorld{};
        bool absoluteDataIds{};
        std::uint32_t reserved{};
    };

    [[nodiscard]] std::expected<LegacySequenceHeader, SequenceError>
    decodeLegacySequenceHeader(std::span<const std::byte> bytes);

    struct SequenceGroupingData {};
    struct SequenceIndirectData { DataId subsequenceDataId{}; };
    struct SequenceBitmapData { DataId bitmapDataId{}; };
    struct SequenceModelData
    {
        DataId modelDataId{};
        DataId textureMapDataId{};
        DataId jointPositionsDataId{};
    };
    struct SequenceSoundData { DataId soundDataId{}; };
    struct SequenceMeshData { DataId modelDataId{}; };

    using LegacySequenceData = std::variant<
        SequenceGroupingData,
        SequenceIndirectData,
        SequenceBitmapData,
        SequenceModelData,
        SequenceSoundData,
        SequenceMeshData>;

    struct LegacySequenceRecord
    {
        ChunkInfo chunk;
        LegacySequenceHeader header;
        LegacySequenceData data;
        std::size_t subchunksOffset{};
    };

    // Descend au prochain chunk et decode uniquement sa partie fixe prouvee :
    // grouping(1), indirect(2), bitmap(3), model(4), sound(5), mesh(9).
    // En succes, le reader reste dans ce chunk au debut des sous-chunks.
    // En erreur, position/niveau/ownership du reader sont inchanges.
    // Le resultat contient des valeurs, sans vue empruntee. La possession du
    // payload reste celle du LegacyChunkReader (span ou SharedDataBytes).
    // Les sous-chunks et l'execution/timing ne sont pas valides ici. Video,
    // camera, preloader, tweeker et tout autre ID sont explicitement refuses.
    [[nodiscard]] std::expected<LegacySequenceRecord, SequenceError>
    readLegacySequenceRecord(LegacyChunkReader& reader);

    // Semantique du loader L_Seqncr : references relatives a l'item DAT qui
    // contient ce record, et non a un numero de groupe fixe. Conserver aussi
    // le DataID brut : sound notamment teste le zero AVANT ce remappage pour
    // choisir un fichier externe (L_Seqncr.cpp:4471-4489).
    [[nodiscard]] constexpr DataId resolveSequenceDataId(
        const LegacySequenceHeader& header,
        DataId rawId,
        DataId containingSequenceId) noexcept
    {
        return header.absoluteDataIds ? rawId :
            idWithGroupFromParent(rawId, containingSequenceId);
    }
}
