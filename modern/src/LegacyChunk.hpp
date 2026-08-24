#pragma once

#include "LegacyDataArchive.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace monopoly::data
{
    enum class ChunkErrorCode
    {
        None,
        EndOfParent,
        ChunkNotFound,
        HeaderTruncated,
        InvalidSize,
        ChunkPastParent,
        InvalidId,
        MaximumDepth,
        AlreadyAtRoot,
        InvalidLevel,
        PositionOutOfRange
    };


    struct ChunkError
    {
        ChunkErrorCode code{ ChunkErrorCode::None };
        std::size_t offset{};
        std::string detail;
    };


    [[nodiscard]] std::string_view chunkErrorCodeName(
        ChunkErrorCode code) noexcept;


    struct ChunkInfo
    {
        std::uint8_t id{};
        std::size_t headerOffset{};
        std::size_t dataOffset{};
        std::size_t dataSize{};
        std::size_t endOffset{};
    };


    class LegacyChunkReader final
    {
    public:
        static constexpr std::size_t PhysicalHeaderSize = 4;
        static constexpr std::size_t MaximumLevels = 8;
        static constexpr std::uint8_t NullStandardId = 0;
        static constexpr std::uint8_t NullInContextId = 128;

        // Le constructeur span est non-owning. Le constructeur SharedDataBytes
        // conserve explicitement le payload DAT pendant toute la lecture.
        explicit LegacyChunkReader(std::span<const std::byte> bytes) noexcept;
        explicit LegacyChunkReader(SharedDataBytes bytes) noexcept;

        // findId == 0 selectionne le prochain sibling et rejette alors les
        // sentinelles 0/128. Une recherche precise peut les franchir, comme
        // LE_CHUNK_Descend, mais les rejette si la sentinelle est selectionnee.
        [[nodiscard]] std::expected<ChunkInfo, ChunkError>
        descend(std::uint8_t findId = NullStandardId);

        [[nodiscard]] std::expected<void, ChunkError> ascend();

        [[nodiscard]] std::expected<std::span<const std::byte>, ChunkError>
        map(std::size_t requestedBytes);

        [[nodiscard]] std::expected<std::size_t, ChunkError>
        read(std::span<std::byte> destination);

        [[nodiscard]] std::expected<void, ChunkError>
        seek(std::size_t absoluteOffset);

        [[nodiscard]] std::size_t currentOffset() const noexcept;
        [[nodiscard]] std::size_t level() const noexcept;
        [[nodiscard]] std::uint8_t idForLevel(std::size_t level) const noexcept;
        [[nodiscard]] std::size_t dataSizeForLevel(
            std::size_t level) const noexcept;
        [[nodiscard]] std::size_t dataStartForLevel(
            std::size_t level) const noexcept;
        [[nodiscard]] std::size_t remaining() const noexcept;

    private:
        struct LevelState
        {
            std::size_t dataStart{};
            std::size_t end{};
            std::uint8_t id{};
        };

        [[nodiscard]] ChunkError error(
            ChunkErrorCode code,
            std::string detail,
            std::size_t offset) const;

        SharedDataBytes owner_;
        std::span<const std::byte> bytes_;
        std::array<LevelState, MaximumLevels> levels_{};
        std::size_t level_{};
        std::size_t currentOffset_{};
    };


    // Equivalent RAII de LE_CHUNK_ReadFromDataID : verifie le type CNK,
    // charge par le registre DATA et conserve la lease partagee dans le
    // reader, y compris si la banque est ensuite demontee.
    [[nodiscard]] std::expected<LegacyChunkReader, DataError>
    openLegacyChunkReader(
        const DataBankRegistry& registry,
        DataId id);
}
