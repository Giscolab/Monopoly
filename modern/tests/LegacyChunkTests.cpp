#include "LegacyChunk.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

namespace
{
    using monopoly::data::ChunkErrorCode;
    using monopoly::data::DataBytes;
    using monopoly::data::LegacyChunkReader;
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


    [[nodiscard]] std::uint8_t byteValue(std::byte value) noexcept
    {
        return std::to_integer<std::uint8_t>(value);
    }


    void appendChunk(
        DataBytes& destination,
        std::uint8_t id,
        std::span<const std::byte> payload)
    {
        const std::size_t totalSize =
            LegacyChunkReader::PhysicalHeaderSize + payload.size();

        if (totalSize > 0x00FF'FFFFU)
        {
            std::terminate();
        }

        destination.push_back(
            static_cast<std::byte>(totalSize & 0xFFU));
        destination.push_back(
            static_cast<std::byte>((totalSize >> 8U) & 0xFFU));
        destination.push_back(
            static_cast<std::byte>((totalSize >> 16U) & 0xFFU));
        destination.push_back(static_cast<std::byte>(id));
        destination.insert(
            destination.end(),
            payload.begin(),
            payload.end());
    }


    [[nodiscard]] DataBytes makeChunk(
        std::uint8_t id,
        std::span<const std::byte> payload = {})
    {
        DataBytes result;
        appendChunk(result, id, payload);
        return result;
    }


    [[nodiscard]] bool hasError(
        const std::expected<monopoly::data::ChunkInfo,
            monopoly::data::ChunkError>& result,
        ChunkErrorCode expectedCode)
    {
        return !result && result.error().code == expectedCode;
    }


    void testPhysicalHeaderAndLarge24BitSize()
    {
        DataBytes payload(65'536, std::byte{ 0x5A });
        const DataBytes bytes = makeChunk(0x2A, payload);

        expect(
            byteValue(bytes[0]) == 0x04 &&
                byteValue(bytes[1]) == 0x00 &&
                byteValue(bytes[2]) == 0x01 &&
                byteValue(bytes[3]) == 0x2A,
            "physical header stores total size on three little-endian bytes"
        );

        LegacyChunkReader reader(bytes);
        const auto chunk = reader.descend();

        expect(chunk.has_value(), "large 24-bit chunk header is accepted");

        if (!chunk)
        {
            return;
        }

        expect(chunk->id == 0x2A, "chunk ID is the fourth header byte");
        expect(
            chunk->headerOffset == 0 &&
                chunk->dataOffset == 4 &&
                chunk->dataSize == payload.size() &&
                chunk->endOffset == bytes.size(),
            "ChunkInfo excludes the physical header from data size"
        );
    }


    void testSiblingsSearchAndAscend()
    {
        const std::array firstPayload{
            std::byte{ 0x11 },
            std::byte{ 0x12 }
        };
        const std::array secondPayload{
            std::byte{ 0x21 },
            std::byte{ 0x22 },
            std::byte{ 0x23 }
        };

        DataBytes bytes;
        appendChunk(bytes, 3, firstPayload);
        appendChunk(bytes, 9, secondPayload);

        LegacyChunkReader sequential(bytes);
        const auto first = sequential.descend();

        expect(
            first && first->id == 3 && sequential.level() == 1,
            "descend without ID enters the next sibling"
        );

        if (first)
        {
            const auto mapped = sequential.map(99);
            expect(
                mapped && mapped->size() == firstPayload.size() &&
                    std::ranges::equal(*mapped, firstPayload),
                "map clamps an oversized request to current chunk data"
            );
            expect(
                sequential.remaining() == 0,
                "mapping advances to the child end"
            );
        }

        const auto ascend = sequential.ascend();
        expect(
            ascend.has_value() && sequential.level() == 0 &&
                sequential.currentOffset() == 6,
            "ascend lands immediately after the completed child"
        );

        const auto second = sequential.descend();
        expect(
            second && second->id == 9 && second->headerOffset == 6,
            "next descend enters the following sibling"
        );

        LegacyChunkReader searched(bytes);
        const auto found = searched.descend(9);
        expect(
            found && found->id == 9 && found->headerOffset == 6,
            "descend(findId) skips complete siblings"
        );

        LegacyChunkReader missing(bytes);
        const auto notFound = missing.descend(42);
        expect(
            hasError(notFound, ChunkErrorCode::ChunkNotFound) &&
                missing.currentOffset() == bytes.size(),
            "missing requested ID reports ChunkNotFound after sibling scan"
        );
        expect(
            hasError(missing.descend(), ChunkErrorCode::EndOfParent),
            "a sequential descend at parent end reports EndOfParent"
        );
    }


    void testNestingReadSeekAndQueries()
    {
        const std::array leafPayload{
            std::byte{ 0xA1 },
            std::byte{ 0xA2 },
            std::byte{ 0xA3 }
        };
        const DataBytes child = makeChunk(7, leafPayload);
        const DataBytes outer = makeChunk(5, child);

        LegacyChunkReader reader(outer);
        const auto parent = reader.descend();
        const auto leaf = reader.descend();

        expect(
            parent && leaf && reader.level() == 2,
            "nested chunks can be descended from their parent data"
        );

        if (!parent || !leaf)
        {
            return;
        }

        expect(
            reader.idForLevel(0) == 0 &&
                reader.idForLevel(1) == 5 &&
                reader.idForLevel(2) == 7 &&
                reader.idForLevel(3) == 0,
            "level IDs expose root and active ancestors only"
        );
        expect(
            reader.dataStartForLevel(1) == parent->dataOffset &&
                reader.dataStartForLevel(2) == leaf->dataOffset &&
                reader.dataStartForLevel(3) == 0,
            "level data-start queries use absolute offsets"
        );
        expect(
            reader.dataSizeForLevel(1) == child.size() &&
                reader.dataSizeForLevel(2) == leafPayload.size() &&
                reader.dataSizeForLevel(3) == 0,
            "level data-size queries remain bounded to active levels"
        );

        std::array<std::byte, 5> destination{};
        std::ranges::fill(destination, std::byte{ 0xEE });
        const auto amount = reader.read(destination);

        expect(
            amount && *amount == leafPayload.size(),
            "read reports the clamped number of copied bytes"
        );
        expect(
            std::ranges::equal(
                destination | std::views::take(leafPayload.size()),
                leafPayload) &&
                destination[3] == std::byte{ 0xEE } &&
                destination[4] == std::byte{ 0xEE },
            "read copies only remaining chunk bytes"
        );

        expect(
            reader.seek(leaf->dataOffset).has_value() &&
                reader.remaining() == leafPayload.size(),
            "seek accepts the first byte of current chunk data"
        );
        expect(
            reader.seek(leaf->endOffset).has_value() &&
                reader.remaining() == 0,
            "seek accepts the inclusive one-past-data end position"
        );

        const auto below = reader.seek(leaf->dataOffset - 1U);
        const auto above = reader.seek(leaf->endOffset + 1U);
        expect(
            !below && below.error().code ==
                ChunkErrorCode::PositionOutOfRange,
            "seek rejects a position before current chunk data"
        );
        expect(
            !above && above.error().code ==
                ChunkErrorCode::PositionOutOfRange,
            "seek rejects a position after current chunk data"
        );

        expect(reader.ascend().has_value(), "first ascend leaves leaf");
        expect(reader.ascend().has_value(), "second ascend leaves parent");
        const auto aboveRoot = reader.ascend();
        expect(
            !aboveRoot && aboveRoot.error().code ==
                ChunkErrorCode::AlreadyAtRoot,
            "ascend rejects moving above the root container"
        );
    }


    void testNullSiblingSearchSemantics()
    {
        DataBytes bytes;
        appendChunk(bytes, 0, std::span<const std::byte>{});
        appendChunk(bytes, 128, std::span<const std::byte>{});
        appendChunk(bytes, 9, std::span<const std::byte>{});

        LegacyChunkReader searched(bytes);
        const auto found = searched.descend(9);
        expect(
            found && found->id == 9 && found->headerOffset == 8,
            "precise ID search skips well-formed null siblings 0 and 128"
        );

        LegacyChunkReader next(bytes);
        const auto selectedNull = next.descend();
        expect(
            hasError(selectedNull, ChunkErrorCode::InvalidId) &&
                selectedNull.error().offset == 0 &&
                next.currentOffset() == 0,
            "sequential next rejects a selected null sibling"
        );

        LegacyChunkReader selectedContextNull(bytes);
        const auto explicitNull = selectedContextNull.descend(128);
        expect(
            hasError(explicitNull, ChunkErrorCode::InvalidId) &&
                explicitNull.error().offset == 4 &&
                selectedContextNull.currentOffset() == 4,
            "precise search rejects ID 128 when that sentinel is selected"
        );
    }


    void testMalformedBoundariesAndIds()
    {
        LegacyChunkReader empty(std::span<const std::byte>{});
        expect(
            hasError(empty.descend(), ChunkErrorCode::EndOfParent),
            "empty root reports EndOfParent"
        );

        for (std::size_t count = 1; count < 4; ++count)
        {
            const DataBytes truncated(count, std::byte{ 0 });
            LegacyChunkReader reader(truncated);
            const auto result = reader.descend();
            expect(
                hasError(result, ChunkErrorCode::HeaderTruncated) &&
                    result.error().offset == 0,
                "one-to-three trailing bytes report HeaderTruncated"
            );
        }

        for (const std::uint32_t invalidSize : { 0U, 3U })
        {
            DataBytes bytes
            {
                static_cast<std::byte>(invalidSize & 0xFFU),
                static_cast<std::byte>((invalidSize >> 8U) & 0xFFU),
                static_cast<std::byte>((invalidSize >> 16U) & 0xFFU),
                std::byte{ 1 }
            };
            LegacyChunkReader reader(bytes);
            expect(
                hasError(reader.descend(), ChunkErrorCode::InvalidSize),
                "total chunk size below four is rejected"
            );
        }

        const DataBytes pastParent
        {
            std::byte{ 5 },
            std::byte{ 0 },
            std::byte{ 0 },
            std::byte{ 1 }
        };
        LegacyChunkReader pastReader(pastParent);
        expect(
            hasError(pastReader.descend(), ChunkErrorCode::ChunkPastParent),
            "chunk ending beyond parent boundary is rejected"
        );

        for (const std::uint8_t invalidId : { std::uint8_t{ 0 },
                std::uint8_t{ 128 } })
        {
            const DataBytes bytes = makeChunk(invalidId);
            LegacyChunkReader reader(bytes);
            expect(
                hasError(reader.descend(), ChunkErrorCode::InvalidId),
                "IDs 0 and 128 are rejected as null sentinels"
            );
        }

        const DataBytes contextual = makeChunk(129);
        LegacyChunkReader contextualReader(contextual);
        const auto accepted = contextualReader.descend();
        expect(
            accepted && accepted->id == 129,
            "ID 129 is valid because its low seven bits are non-zero"
        );
    }


    void testMaximumDepth()
    {
        DataBytes nested = makeChunk(8);

        for (std::uint8_t id = 7; id > 0; --id)
        {
            nested = makeChunk(id, nested);
        }

        LegacyChunkReader reader(nested);
        bool firstSevenAccepted = true;

        for (std::size_t expectedLevel = 1; expectedLevel < 8;
            ++expectedLevel)
        {
            const auto result = reader.descend();
            firstSevenAccepted =
                firstSevenAccepted && result.has_value() &&
                reader.level() == expectedLevel;
        }

        expect(
            firstSevenAccepted && reader.level() == 7,
            "seven child levels fit beside root in the eight-level stack"
        );

        const auto tooDeep = reader.descend();
        expect(
            hasError(tooDeep, ChunkErrorCode::MaximumDepth) &&
                reader.level() == 7,
            "eighth child level is rejected without changing reader level"
        );
    }


    void testOwningReaderLease()
    {
        const std::array payload{
            std::byte{ 0x31 },
            std::byte{ 0x32 }
        };
        auto mutableOwner = std::make_shared<DataBytes>(makeChunk(4, payload));
        SharedDataBytes sharedOwner = mutableOwner;
        LegacyChunkReader reader(sharedOwner);

        sharedOwner.reset();
        mutableOwner.reset();

        const auto chunk = reader.descend();
        const auto mapped = reader.map(payload.size());
        expect(
            chunk && mapped && std::ranges::equal(*mapped, payload),
            "owning reader keeps DAT payload alive after caller releases it"
        );
    }
}


int main()
{
    std::cout
        << "Monopoly legacy chunk tests\n"
        << "===========================\n";

    testPhysicalHeaderAndLarge24BitSize();
    testSiblingsSearchAndAscend();
    testNestingReadSeekAndQueries();
    testNullSiblingSearchSemantics();
    testMalformedBoundariesAndIds();
    testMaximumDepth();
    testOwningReaderLease();

    std::cout << '\n';

    if (failures != 0)
    {
        std::cerr << failures << " legacy chunk test(s) failed.\n";
        return 1;
    }

    std::cout << "All legacy chunk tests passed.\n";
    return 0;
}
