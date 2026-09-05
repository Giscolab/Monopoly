#include "SequenceChildSchedule.hpp"

#include <utility>

namespace monopoly::sequence
{
    SequenceChildSchedule::SequenceChildSchedule(
        std::shared_ptr<const Description> description)
        : description_(std::move(description)) {}

    std::expected<SequenceChildSchedule, ChildScheduleError>
    SequenceChildSchedule::read(data::SharedDataBytes bytes,
        data::DataId containingDataId, std::size_t maximumRecords)
    {
        return readChildren(data::LegacyChunkReader(std::move(bytes)),
            containingDataId, maximumRecords);
    }

    std::expected<SequenceChildSchedule, ChildScheduleError>
    SequenceChildSchedule::readChildren(data::LegacyChunkReader reader,
        data::DataId containingDataId, std::size_t maximumRecords)
    {
        auto description = std::make_shared<Description>(Description{reader, containingDataId, {}});
        while (reader.remaining() != 0)
        {
            auto probe = reader;
            auto chunk = probe.descend();
            if (!chunk) return std::unexpected(ChildScheduleError{ chunk.error() });
            // L_Seqncr.cpp:5300 tests 1 <= ID < SEQ_MAX (11).
            if (chunk->id >= 1 && chunk->id < 11)
            {
                if (description->records.size() >= maximumRecords)
                    return std::unexpected(ChildScheduleError{ data::SequenceError{
                        data::SequenceErrorCode::RecordLimitExceeded,
                        chunk->headerOffset, "child sequence record budget exceeded",
                        std::nullopt } });
                auto record = data::readLegacySequenceRecord(reader);
                if (!record)
                    return std::unexpected(ChildScheduleError{ record.error() });
                description->records.push_back(std::move(*record));
                auto ascended = reader.ascend();
                if (!ascended)
                    return std::unexpected(ChildScheduleError{ ascended.error() });
            }
            else
            {
                // Not an execution claim for attributes: preserve in reader_.
                auto skipped = reader.seek(chunk->endOffset);
                if (!skipped)
                    return std::unexpected(ChildScheduleError{ skipped.error() });
            }
        }
        return SequenceChildSchedule(std::move(description));
    }

    std::span<const data::LegacySequenceRecord>
    SequenceChildSchedule::records() const noexcept { return description_->records; }
    data::DataId SequenceChildSchedule::containingDataId() const noexcept
    { return description_->containingDataId; }
    std::size_t SequenceChildSchedule::nextIndex() const noexcept { return nextIndex_; }

    std::expected<std::vector<std::size_t>, data::SequenceError>
    SequenceChildSchedule::select(std::optional<std::int32_t> previous,
        std::int32_t current)
    {
        if (previous && current < *previous)
            return std::unexpected(data::SequenceError{
                data::SequenceErrorCode::InvalidClockRange, description_->reader.currentOffset(),
                "child scan cannot run backwards; rewind and rescan from minus infinity",
                std::nullopt });
        std::vector<std::size_t> selected;
        auto next = nextIndex_;
        for (; next < description_->records.size(); ++next)
        {
            const auto& header = description_->records[next].header;
            if (header.parentStartTime > current) break;
            const auto end = static_cast<std::int64_t>(header.parentStartTime) +
                header.endTime;
            if ((!previous || header.parentStartTime > *previous) &&
                (header.endingAction != 1 || header.endTime == 0 || end > current))
                selected.push_back(next);
        }
        nextIndex_ = next;
        return selected;
    }

    void SequenceChildSchedule::rewind() noexcept { nextIndex_ = 0; }

    std::expected<data::LegacyChunkReader, data::ChunkError>
    SequenceChildSchedule::readerForChild(std::size_t index) const
    {
        if (index >= description_->records.size())
            return std::unexpected(data::ChunkError{
                data::ChunkErrorCode::PositionOutOfRange, description_->reader.currentOffset(),
                "child record index is out of range" });
        auto reader = description_->reader;
        const auto seek = reader.seek(description_->records[index].chunk.headerOffset);
        if (!seek) return std::unexpected(seek.error());
        return reader;
    }

    std::expected<SequenceChildSchedule, ChildScheduleError>
    openSequenceChildSchedule(const data::DataBankRegistry& registry,
        data::DataId parentDataId, std::size_t parentOffset, std::size_t maximumRecords)
    {
        auto reader = data::openLegacyChunkReader(registry, parentDataId);
        if (!reader) return std::unexpected(ChildScheduleError{ reader.error() });
        const auto seek = reader->seek(parentOffset);
        if (!seek) return std::unexpected(ChildScheduleError{ seek.error() });
        auto parent = data::readLegacySequenceRecord(*reader);
        if (!parent) return std::unexpected(ChildScheduleError{ parent.error() });
        if (const auto* indirect = std::get_if<data::SequenceIndirectData>(&parent->data))
        {
            const auto id = data::resolveSequenceDataId(parent->header,
                indirect->subsequenceDataId, parentDataId);
            // Absolute zero means no children. Relative zero is group:tag0,
            // not empty (the legacy remapping macro must run first).
            if (id == 0)
                return SequenceChildSchedule::read({}, id, maximumRecords);
            if (id == parentDataId)
            {
                // AddNewlyBornChildren (5269-5279) chooses parent bounds by
                // DATA identity, even for an indirect sequence. Its seek to
                // zero then fails (L_Chunk.cpp:2497-2511). Do not accidentally
                // turn this historically invalid case into recursive playback.
                return std::unexpected(ChildScheduleError{ data::ChunkError{
                    data::ChunkErrorCode::PositionOutOfRange, 0,
                    "same-item indirect child list starts outside its parent chunk" } });
            }
            auto external = data::openLegacyChunkReader(registry, id);
            if (!external) return std::unexpected(ChildScheduleError{ external.error() });
            return SequenceChildSchedule::readChildren(std::move(*external), id, maximumRecords);
        }
        return SequenceChildSchedule::readChildren(std::move(*reader), parentDataId, maximumRecords);
    }
}
