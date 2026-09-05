#include "SequenceChildSchedule.hpp"
#include "SequenceClock.hpp"
#include "LegacyDataArchiveBuilder.hpp"

#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
    using namespace monopoly::data;
    using namespace monopoly::sequence;
    int failures{};
    void expect(bool condition, std::string_view detail)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << detail << '\n';
        if (!condition) ++failures;
    }
    void word(DataBytes& bytes, std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<std::byte>((value >> shift) & 255U));
    }
    void append(DataBytes& into, const DataBytes& bytes)
    { into.insert(into.end(), bytes.begin(), bytes.end()); }
    DataBytes chunk(std::uint8_t id, const DataBytes& payload)
    {
        DataBytes bytes;
        word(bytes, (static_cast<std::uint32_t>(id) << 24U) |
            static_cast<std::uint32_t>(payload.size() + 4));
        append(bytes, payload);
        return bytes;
    }
    // Source-defined synthetic common fields, no retail payload.
    DataBytes grouping(std::int32_t start, std::int32_t end,
        std::uint8_t action = 1, const DataBytes& children = {})
    {
        DataBytes payload;
        word(payload, static_cast<std::uint32_t>(start) & 0x00FF'FFFFU);
        word(payload, (static_cast<std::uint32_t>(end) & 0x00FF'FFFFU) | 0x4100'0000U);
        word(payload, action);
        append(payload, children);
        return chunk(1, payload);
    }
    DataBytes indirect(DataId target, bool absolute, const DataBytes& children = {})
    {
        DataBytes payload;
        word(payload, 0); word(payload, 0x0100'0000U);
        word(payload, 1U | (absolute ? 16U : 0U)); word(payload, target);
        append(payload, children);
        return chunk(2, payload);
    }
    SharedDataBytes owned(DataBytes bytes)
    { return std::make_shared<const DataBytes>(std::move(bytes)); }

    void testSelection()
    {
        DataBytes bytes = chunk(40, DataBytes{std::byte{0xA5}}); // opaque attribute
        append(bytes, grouping(-3, 0));  // infinite, survives an old start
        append(bytes, grouping(0, 4));   // expired at current=4
        append(bytes, grouping(0, 4, 2)); // held end is still eligible
        append(bytes, grouping(0, 4, 3)); // loop is still eligible
        append(bytes, grouping(5, 10));
        auto schedule = SequenceChildSchedule::read(owned(bytes), packDataId(8, 3)).value();
        expect(schedule.records().size() == 5, "attributes do not become child sequences");
        auto selection = schedule.select(std::nullopt, 4);
        expect(selection && *selection == std::vector<std::size_t>{0, 2, 3} &&
            schedule.nextIndex() == 4,
            "first scan includes old infinite/held/loop children but skips expired stops");
        selection = schedule.select(4, 5);
        expect(selection && *selection == std::vector<std::size_t>{4},
            "child starts on its exact upper time boundary");
        selection = schedule.select(5, 9);
        expect(selection && selection->empty(), "a child is never selected twice in one pass");
        const auto before = schedule.nextIndex();
        selection = schedule.select(9, 8);
        expect(!selection && selection.error().code == SequenceErrorCode::InvalidClockRange &&
            schedule.nextIndex() == before, "invalid scan preserves the cursor");
        schedule.rewind();
        selection = schedule.select(0, 5);
        expect(selection && *selection == std::vector<std::size_t>{4},
            "lower time boundary is exclusive even after an explicit rewind");

        DataBytes unordered = grouping(10, 0);
        append(unordered, grouping(0, 0));
        schedule = SequenceChildSchedule::read(owned(unordered), packDataId(8, 3)).value();
        selection = schedule.select(std::nullopt, 0);
        expect(selection && selection->empty() && schedule.nextIndex() == 0,
            "source order is preserved: first future child blocks later disk records");
        selection = schedule.select(0, 10);
        expect(selection && *selection == std::vector<std::size_t>{0},
            "an unsorted child outside the current interval is skipped, not rescheduled");
    }

    void testClockConnectionAndOwnership()
    {
        auto schedule = SequenceChildSchedule::read(
            owned(grouping(0, 0)), packDataId(8, 3)).value();
        auto reader = schedule.readerForChild(0).value();
        auto parent = readLegacySequenceRecord(reader).value();
        parent.header.endTime = 4;
        parent.header.endingAction = 3;
        auto clock = SequenceClock::start(parent).value();
        auto update = clock.update(100).value();
        auto selection = schedule.select(update.previousClock, update.clock);
        expect(selection && selection->size() == 1, "initial clock event births time-zero children");
        update = clock.update(104).value();
        expect(update.restartChildren, "loop requests child destruction/rebirth");
        schedule.rewind();
        selection = schedule.select(std::nullopt, update.clock);
        expect(selection && selection->size() == 1, "loop rewind selects time-zero children again");
        expect(!schedule.readerForChild(1), "invalid child index is bounded");

        // Independent reader owns bytes even after schedule move/destruction.
        auto retained = schedule.readerForChild(0).value();
        {
            auto moved = std::move(schedule);
            expect(moved.records().size() == 1, "moving a schedule preserves its records");
        }
        const auto decoded = readLegacySequenceRecord(retained);
        expect(decoded && decoded->header.parentStartTime == 0,
            "child reader owns the original bytes beyond schedule lifetime");
    }

    void testErrors()
    {
        auto result = SequenceChildSchedule::read(owned(grouping(0, 1)), 1, 0);
        expect(!result && std::holds_alternative<SequenceError>(result.error()) &&
            std::get<SequenceError>(result.error()).code == SequenceErrorCode::RecordLimitExceeded,
            "zero record budget rejects a child before allocation");
        auto bad = grouping(0, 1);
        bad.pop_back();
        result = SequenceChildSchedule::read(owned(bad), 1);
        expect(!result && std::holds_alternative<ChunkError>(result.error()),
            "truncated child propagates physical chunk bounds error");
        result = SequenceChildSchedule::read(owned(chunk(6, DataBytes(12))), 1);
        expect(!result && std::holds_alternative<SequenceError>(result.error()) &&
            std::get<SequenceError>(result.error()).code == SequenceErrorCode::UnsupportedRecord,
            "unported video child is refused even before its scheduled start");
        result = SequenceChildSchedule::read(owned(chunk(0, {})), 1);
        expect(!result, "null sentinel is not silently accepted as an attribute");
        result = SequenceChildSchedule::read(owned(chunk(40, {})), 1, 0);
        expect(result && result->records().empty(), "empty child list accepts attributes only");
    }

    void testRegistryConnection()
    {
        // Fixture directory is created atomically inside the CTest build cwd.
        const auto directory = std::filesystem::current_path() /
            ("sequence-child-fixture-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(directory))
            throw std::runtime_error("fixture directory collision");
        struct Cleanup
        {
            std::filesystem::path root;
            ~Cleanup()
            {
                std::error_code ignored;
                std::filesystem::remove(root / "children.dat", ignored);
                std::filesystem::remove(root, ignored);
            }
        } cleanup{directory};

        DataBytes direct = grouping(0, 20, 3, grouping(2, 0));
        append(direct, grouping(99, 0)); // sibling of parent, not its child
        const std::array items{
            ArchiveBuildItem{LegacyDataType::Chunky, grouping(7, 0)},
            ArchiveBuildItem{LegacyDataType::Chunky, direct},
            ArchiveBuildItem{LegacyDataType::Chunky, indirect(0, false, grouping(77, 0))},
            ArchiveBuildItem{LegacyDataType::Chunky, indirect(packDataId(9, 0), true)},
            ArchiveBuildItem{LegacyDataType::Chunky, indirect(0, true, grouping(77, 0))},
            ArchiveBuildItem{LegacyDataType::Native, grouping(7, 0)},
            ArchiveBuildItem{LegacyDataType::Chunky, indirect(5, false)},
            ArchiveBuildItem{LegacyDataType::Chunky, indirect(7, false)}
        };
        const auto written = writeLegacyDataArchive(directory / "children.dat", items);
        expect(written.has_value(), "synthetic child CNK list is wrapped by the existing DAT writer");
        if (!written) return;
        DataBankRegistry registry;
        const auto mounted = registry.mount(directory / "children.dat", 8);
        const auto external = registry.mount(directory / "children.dat", 9);
        expect(mounted && external, "test banks mount in two groups");
        if (!mounted || !external) return;
        auto directSchedule = openSequenceChildSchedule(registry, packDataId(8, 1), 0).value();
        expect(directSchedule.records().size() == 1 &&
            directSchedule.records()[0].header.parentStartTime == 2,
            "direct children are bounded by the parent, not the whole DAT item");
        auto relative = openSequenceChildSchedule(registry, packDataId(8, 2), 0).value();
        expect(relative.containingDataId() == packDataId(8, 0) &&
            relative.records().size() == 1 && relative.records()[0].header.parentStartTime == 7,
            "indirect relative tag zero overrides inline children and retains parent bank");
        const auto absolute = openSequenceChildSchedule(registry, packDataId(8, 3), 0);
        expect(absolute && absolute->containingDataId() == packDataId(9, 0),
            "absolute indirect reference selects its own group");
        const auto empty = openSequenceChildSchedule(registry, packDataId(8, 4), 0);
        expect(empty && empty->records().empty(),
            "absolute empty ID suppresses even inline indirect children");
        auto error = openSequenceChildSchedule(registry, packDataId(8, 6), 0);
        expect(!error && std::holds_alternative<DataError>(error.error()) &&
            std::get<DataError>(error.error()).code == DataErrorCode::TypeMismatch,
            "indirect target must have CNK type, not just CNK-shaped bytes");
        error = openSequenceChildSchedule(registry, packDataId(10, 0), 0);
        expect(!error && std::holds_alternative<DataError>(error.error()),
            "missing parent bank keeps the DATA diagnostic");
        error = openSequenceChildSchedule(registry, packDataId(8, 1), 9999);
        expect(!error && std::holds_alternative<ChunkError>(error.error()),
            "invalid parent offset is checked before decoding");
        const auto self = openSequenceChildSchedule(registry, packDataId(8, 7), 0);
        expect(!self && std::holds_alternative<ChunkError>(self.error()) &&
            std::get<ChunkError>(self.error()).code == ChunkErrorCode::PositionOutOfRange,
            "same-item indirect reference retains the source parent-boundary rejection");
        registry.clear();
        (*mounted)->close(); (*external)->close();
        auto heldReader = directSchedule.readerForChild(0).value();
        const auto retained = readLegacySequenceRecord(heldReader);
        expect(retained && retained->header.parentStartTime == 2,
            "schedule lease survives bank unmount and explicit archive close");
    }
}

int main()
{
    try
    {
        testSelection();
        testClockConnectionAndOwnership();
        testErrors();
        testRegistryConnection();
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        ++failures;
    }
    return failures == 0 ? 0 : 1;
}
