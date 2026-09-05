#pragma once

#include "LegacySequence.hpp"

#include <vector>
#include <memory>

namespace monopoly::sequence
{
    using ChildScheduleError = std::variant<
        data::DataError, data::ChunkError, data::SequenceError>;

    // Owned, ordered child definitions and their scan cursor. This extracts
    // AddNewlyBornChildren's selection rule, NOT recursive tree execution.
    // Attribute chunks are skipped only for scheduling: their original bytes
    // remain accessible through the owning reader for the future transform,
    // label and renderer consumers. Unsupported sequence kinds fail explicitly.
    class SequenceChildSchedule final
    {
    public:
        // Reads a whole owning CNK sibling list. Use the registry adapter
        // below for direct or indirect children of a particular parent.
        [[nodiscard]] static std::expected<SequenceChildSchedule, ChildScheduleError>
        read(data::SharedDataBytes bytes, data::DataId containingDataId,
            std::size_t maximumRecords = 65'536);

        [[nodiscard]] std::span<const data::LegacySequenceRecord> records() const noexcept;
        [[nodiscard]] data::DataId containingDataId() const noexcept;
        [[nodiscard]] std::size_t nextIndex() const noexcept;

        // Source interval is (previous, current]. nullopt means minus infinity.
        // Never sorts disk order: the first future child stops the scan. Stop
        // children already past end are skipped; infinite/held/loop children
        // are not. Returns indices into records(), never borrowed pointers.
        [[nodiscard]] std::expected<std::vector<std::size_t>, data::SequenceError>
        select(std::optional<std::int32_t> previous, std::int32_t current);
        void rewind() noexcept;

        // Copy of the owning reader positioned BEFORE this child's header.
        // Can decode the record and its attributes after a DAT bank is closed.
        [[nodiscard]] std::expected<data::LegacyChunkReader, data::ChunkError>
        readerForChild(std::size_t index) const;

    private:
        friend std::expected<SequenceChildSchedule, ChildScheduleError>
        openSequenceChildSchedule(const data::DataBankRegistry&,
            data::DataId, std::size_t, std::size_t);
        [[nodiscard]] static std::expected<SequenceChildSchedule, ChildScheduleError>
        readChildren(data::LegacyChunkReader reader, data::DataId containingDataId,
            std::size_t maximumRecords);
        struct Description
        {
            data::LegacyChunkReader reader;
            data::DataId containingDataId;
            std::vector<data::LegacySequenceRecord> records;
        };
        explicit SequenceChildSchedule(std::shared_ptr<const Description> description);
        // Copies share immutable definitions/bytes; only the scan cursor is
        // mutable. A runtime birth must not duplicate whole sibling payloads.
        std::shared_ptr<const Description> description_;
        std::size_t nextIndex_{};
    };

    // Decodes the parent at its DAT-relative offset. Direct children remain
    // inside that parent; indirect children come from the resolved CNK item,
    // at offset zero, overriding any inline children (L_Seqncr.cpp:4293-4303).
    // Same-item indirect targets retain the source's parent-boundary failure.
    // Only one level is loaded: cross-item cycles are NOT expanded recursively.
    [[nodiscard]] std::expected<SequenceChildSchedule, ChildScheduleError>
    openSequenceChildSchedule(const data::DataBankRegistry& registry,
        data::DataId parentDataId, std::size_t parentOffset,
        std::size_t maximumRecords = 65'536);
}
