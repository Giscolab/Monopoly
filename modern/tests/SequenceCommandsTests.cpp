#include "SequenceCommands.hpp"
#include "LegacyDataArchiveBuilder.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly::data;
    using namespace monopoly::sequence;

    int failures{};
    void expect(bool condition, std::string_view text)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << text << '\n';
        if (!condition) ++failures;
    }
    void word(DataBytes& bytes, std::uint32_t value)
    {
        for (unsigned shift = 0; shift != 32; shift += 8)
            bytes.push_back(static_cast<std::byte>((value >> shift) & 255U));
    }
    void append(DataBytes& target, const DataBytes& source)
    { target.insert(target.end(), source.begin(), source.end()); }
    DataBytes chunk(std::uint8_t id, const DataBytes& payload)
    {
        DataBytes result;
        word(result, (static_cast<std::uint32_t>(id) << 24U) |
            static_cast<std::uint32_t>(payload.size() + 4));
        append(result, payload);
        return result;
    }
    DataBytes sequence(std::int32_t start, std::int32_t end,
        std::uint8_t action, std::uint8_t priority, const DataBytes& children = {})
    {
        DataBytes payload;
        word(payload, (static_cast<std::uint32_t>(priority) << 24U) |
            (static_cast<std::uint32_t>(start) & 0x00FF'FFFFU));
        word(payload, (4U << 24U) | 0x4000'0000U |
            (static_cast<std::uint32_t>(end) & 0x00FF'FFFFU));
        word(payload, action);
        append(payload, children);
        return chunk(1, payload);
    }
    DataBytes indirect(DataId target)
    {
        DataBytes payload;
        word(payload, 0);
        word(payload, 4U << 24U);
        word(payload, 17U); // ending action Stop + absolute DATA reference
        word(payload, target);
        return chunk(2, payload);
    }

    struct Fixture
    {
        std::filesystem::path root;
        Fixture()
        {
            root = std::filesystem::current_path() /
                ("SequenceCommands-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
            if (!std::filesystem::create_directory(root))
                throw std::runtime_error("sequence command fixture collision");
        }
        ~Fixture()
        {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }
    };

    std::shared_ptr<const SequenceProgram> makeProgram(const std::filesystem::path& path,
        std::span<const ArchiveBuildItem> items, DataTag rootTag = 0)
    {
        const auto written = writeLegacyDataArchive(path, items);
        expect(written.has_value(), "synthetic command DAT is written");
        DataBankRegistry registry;
        auto mounted = registry.mount(path, 2);
        expect(mounted.has_value(), "synthetic command DAT mounts");
        auto loaded = SequenceProgram::load(registry, packDataId(2, rootTag));
        expect(loaded.has_value(), "immutable command program loads");
        return loaded ? *loaded : nullptr;
    }

    void testNestedBatchingAndFifo()
    {
        Fixture fixture;
        DataBytes child;
        append(child, sequence(0, 0, 2, 4));
        const std::array items{ArchiveBuildItem{LegacyDataType::Chunky,
            sequence(0, 10, 1, 0, child)}};
        auto program = makeProgram(fixture.root / "batch.dat", items);
        const auto dataId = packDataId(2, 0);
        SequenceRuntime runtime;
        SequenceCommandQueue commands(runtime);

        expect(commands.collect() == 1 && commands.collect() == 2,
            "CollectCommands nesting increments exactly");
        expect(commands.enqueue(StartSequenceCommand{program, 400}).has_value(),
            "Start command is accepted with its 16-bit command priority");
        expect(commands.updateCycle(0).has_value() && runtime.roots().empty() &&
            commands.pendingCount() == 1,
            "positive collection level holds commands while the runtime still updates");
        expect(commands.execute() == 1 && runtime.roots().empty(),
            "inner ExecuteCommands does not release the batch");
        expect(commands.execute() == 0 && runtime.roots().size() == 1 &&
            runtime.liveNodeCount() == 2 && commands.pendingCount() == 0,
            "outer ExecuteCommands drains FIFO then performs its zero-time update");
        expect(commands.outcomes().size() == 1 &&
            commands.outcomes()[0].kind == SequenceCommandKind::Start &&
            commands.outcomes()[0].startedNode.has_value() && !commands.lastCycleError(),
            "Start outcome and zero-time cycle status remain observable");

        const auto first = runtime.roots().front();
        expect(runtime.inspect(first)->dimensionality == 0 &&
            !runtime.inspect(first)->tweekerTransformApplied,
            "command-created unpositioned grouping retains inherited dimensionality");
        expect(commands.enqueue(SetSequenceEndingActionCommand{
            dataId, 400, 2, false}).has_value(), "SetEndingAction is queued");
        expect(commands.updateCycle(10).has_value() && runtime.inspect(first) &&
            runtime.inspect(first)->clock == 10 && runtime.inspect(first)->timeMultiple == 255,
            "FIFO applies SetEndingAction before the same cycle reaches the end");

        expect(commands.enqueue(StartSequenceCommand{program, 401}).has_value() &&
            commands.enqueue(StartSequenceCommand{program, 401}).has_value(),
            "Start never deduplicates equal DATA ID and priority");
        expect(commands.updateCycle(10).has_value() &&
            runtime.matching(dataId, 401).size() == 2,
            "duplicate top-level instances are both created");
        expect(commands.enqueue(StopSequenceCommand{dataId, 401, false}).has_value() &&
            commands.updateCycle(10).has_value() &&
            runtime.matching(dataId, 401).empty() && runtime.inspect(first),
            "simple Stop removes every matching top-level instance only");
        expect(commands.outcomes().size() == 1 && commands.outcomes()[0].matched == 2,
            "Stop outcome reports both deleted top-level matches");

        SequenceRuntime fifoRuntime;
        SequenceCommandQueue fifo(fifoRuntime);
        (void)fifo.enqueue(SetSequenceEndingActionCommand{dataId, 77, 2, false});
        (void)fifo.enqueue(StartSequenceCommand{program, 77});
        expect(fifo.updateCycle(0).has_value() && fifo.outcomes().size() == 2 &&
            fifo.outcomes()[0].matched == 0,
            "commands execute in submission order, including an unmatched early command");
        expect(fifo.updateCycle(10).has_value() && fifoRuntime.roots().empty(),
            "an early unmatched SetEndingAction is not retroactively applied to later Start");
    }

    void testWholeTreeTargeting()
    {
        Fixture fixture;
        DataBytes children;
        append(children, indirect(packDataId(2, 1)));
        const std::array items{
            ArchiveBuildItem{LegacyDataType::Chunky, sequence(0, 20, 2, 1, children)},
            ArchiveBuildItem{LegacyDataType::Chunky, sequence(0, 20, 2, 6)}};
        auto program = makeProgram(fixture.root / "whole-tree.dat", items);
        SequenceRuntime runtime;
        SequenceCommandQueue commands(runtime);
        (void)commands.enqueue(StartSequenceCommand{program, 9});
        expect(commands.updateCycle(0).has_value() && runtime.liveNodeCount() == 3,
            "indirect nested sequence is instantiated for command targeting");
        const auto nestedId = packDataId(2, 1);
        (void)commands.enqueue(StopSequenceCommand{nestedId, 6, false});
        expect(commands.updateCycle(0).has_value() && runtime.liveNodeCount() == 3 &&
            commands.outcomes()[0].matched == 0,
            "top-only Stop does not search nested sequences");
        (void)commands.enqueue(SetSequenceEndingActionCommand{nestedId, 6, 3, true});
        expect(commands.updateCycle(0).has_value() && commands.outcomes()[0].matched == 1,
            "whole-tree SetEndingAction reaches an exact nested DATA/priority match");
        (void)commands.enqueue(StopSequenceCommand{nestedId, 6, true});
        expect(commands.updateCycle(0).has_value() && runtime.liveNodeCount() == 2 &&
            commands.outcomes()[0].matched == 1,
            "whole-tree Stop deletes the nested match without deleting its ancestors");
    }

    void testCapacityNegativeNestingAndErrors()
    {
        SequenceRuntime emptyRuntime;
        SequenceCommandQueue full(emptyRuntime);
        expect(full.collect() == 1, "capacity test enters collection mode");
        bool allAccepted = true;
        for (std::size_t index = 0; index < SequenceCommandQueue::Capacity; ++index)
            allAccepted = allAccepted &&
                full.enqueue(StopSequenceCommand{static_cast<DataId>(index), 0}).has_value();
        expect(allAccepted, "all slots up to the historical 500-command capacity are accepted");
        const auto overflow = full.enqueue(StopSequenceCommand{999, 0});
        expect(!overflow && overflow.error() == CommandQueueError::QueueFull &&
            full.percentageFull() == 100,
            "the 501st command is rejected and full percentage is exact");
        expect(full.execute() == 0 && full.pendingCount() == 0 &&
            full.outcomes().size() == SequenceCommandQueue::Capacity &&
            full.percentageFull() == 0,
            "Execute drains all 500 commands FIFO without treating unmatched Stop as failure");
        expect(full.execute() == -1 && full.nestingLevel() == -1,
            "extra Execute preserves the historical negative nesting state");

        Fixture fixture;
        const std::array items{ArchiveBuildItem{LegacyDataType::Chunky,
            sequence(0, 20, 2, 0)}};
        auto program = makeProgram(fixture.root / "errors.dat", items);
        expect(!full.enqueue(StartSequenceCommand{}) &&
            !full.enqueue(SetSequenceEndingActionCommand{packDataId(2, 0), 1, 0}),
            "invalid program and ending action are rejected before queue insertion");

        SequenceRuntime limited(RuntimeLimits{1, 8});
        SequenceCommandQueue commands(limited);
        (void)commands.enqueue(StartSequenceCommand{program, 5});
        (void)commands.enqueue(StartSequenceCommand{program, 6});
        (void)commands.enqueue(SetSequenceEndingActionCommand{packDataId(2, 0), 5, 2});
        expect(commands.updateCycle(0).has_value() && commands.outcomes().size() == 3 &&
            !commands.outcomes()[0].error && commands.outcomes()[1].error &&
            commands.outcomes()[1].error->code == RuntimeErrorCode::LiveNodeLimit &&
            commands.outcomes()[2].matched == 1 && limited.roots().size() == 1,
            "one failed command is reported and does not suppress later FIFO commands");
    }
}

int main()
{
    testNestedBatchingAndFifo();
    testWholeTreeTargeting();
    testCapacityNegativeNestingAndErrors();
    std::cout << (failures ? "Sequence command tests FAILED\n" :
        "Sequence command tests passed\n");
    return failures ? 1 : 0;
}
