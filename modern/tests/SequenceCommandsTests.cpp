#include "SequenceCommands.hpp"
#include "LegacyDataArchiveBuilder.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
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
    void real(DataBytes& bytes, float value)
    { word(bytes, std::bit_cast<std::uint32_t>(value)); }
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
    DataBytes offset2D(std::int32_t x, std::int32_t y)
    {
        DataBytes payload;
        word(payload, static_cast<std::uint32_t>(x));
        word(payload, static_cast<std::uint32_t>(y));
        return chunk(130, payload);
    }
    DataBytes offset3D(float x, float y, float z)
    {
        DataBytes payload;
        real(payload, x); real(payload, y); real(payload, z);
        return chunk(133, payload);
    }
    bool near(float left, float right)
    { return std::fabs(left - right) < 0.0001F; }

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

    void testMoveXYReplacementAndRecursiveWorldPropagation()
    {
        Fixture fixture;
        DataBytes child;
        append(child, offset2D(3, 4));
        DataBytes root;
        append(root, offset2D(1, 2));
        append(root, sequence(0, 20, 2, 7, child));
        const std::array items{ArchiveBuildItem{LegacyDataType::Chunky,
            sequence(0, 20, 2, 0, root)}};
        auto program = makeProgram(fixture.root / "move-xy.dat", items);
        const auto dataId = packDataId(2, 0);
        SequenceRuntime runtime;
        SequenceCommandQueue commands(runtime);
        (void)commands.enqueue(StartSequenceCommand{program, 40});
        (void)commands.enqueue(StartSequenceCommand{program, 41});
        expect(commands.updateCycle(0).has_value(),
            "two positioned 2D roots start before movement");

        (void)commands.enqueue(makeMoveXY(dataId, 40, 10, 20));
        expect(commands.updateCycle(0).has_value() && commands.outcomes().size() == 1 &&
            commands.outcomes()[0].kind == SequenceCommandKind::Move &&
            commands.outcomes()[0].matched == 1,
            "MoveXY targets exact DATA ID and command priority");
        const auto moved = runtime.inspect(runtime.matching(dataId, 40).front());
        const auto untouched = runtime.inspect(runtime.matching(dataId, 41).front());
        const auto childView = moved && !moved->children.empty() ?
            runtime.inspect(moved->children.front()) : std::nullopt;
        const auto* local = moved ? std::get_if<Matrix2D>(&moved->localTransform) : nullptr;
        const auto* childWorld = childView ?
            std::get_if<Matrix2D>(&childView->worldTransform) : nullptr;
        const auto* untouchedLocal = untouched ?
            std::get_if<Matrix2D>(&untouched->localTransform) : nullptr;
        expect(local && near(local->values[6], 10) && near(local->values[7], 20) &&
            childWorld && near(childWorld->values[6], 13) && near(childWorld->values[7], 24),
            "MoveXY replaces local state and immediately propagates world state through a cadence-gated child");
        expect(untouchedLocal && near(untouchedLocal->values[6], 1) &&
            near(untouchedLocal->values[7], 2),
            "movement leaves a different command priority untouched");

        (void)commands.enqueue(makeMoveXY(dataId, 40, 0, 0));
        expect(commands.updateCycle(0).has_value(), "zero MoveXY reset executes");
        const auto reset = runtime.inspect(runtime.matching(dataId, 40).front());
        const auto resetChild = reset && !reset->children.empty() ?
            runtime.inspect(reset->children.front()) : std::nullopt;
        const auto* resetLocal = reset ? std::get_if<Matrix2D>(&reset->localTransform) : nullptr;
        const auto* resetWorld = resetChild ?
            std::get_if<Matrix2D>(&resetChild->worldTransform) : nullptr;
        expect(reset && !reset->explicitlyPositioned && resetLocal &&
            near(resetLocal->values[6], 0) && near(resetLocal->values[7], 0) &&
            resetWorld && near(resetWorld->values[6], 3) && near(resetWorld->values[7], 4),
            "MoveXY(0,0) follows the historical null-matrix identity reset");

        expect(commands.updateCycle(20).has_value() &&
            runtime.inspect(runtime.matching(dataId, 40).front())->timeMultiple == 255,
            "2D movement target reaches historical Hold state");
        (void)commands.enqueue(makeMoveXY(dataId, 40, 50, 60));
        expect(commands.updateCycle(20).has_value(),
            "movement forces reevaluation at the same tick while Hold cadence is active");
        const auto held = runtime.inspect(runtime.matching(dataId, 40).front());
        const auto heldChild = held && !held->children.empty() ?
            runtime.inspect(held->children.front()) : std::nullopt;
        const auto* heldWorld = heldChild ?
            std::get_if<Matrix2D>(&heldChild->worldTransform) : nullptr;
        expect(held && held->clock == 20 && held->timeMultiple == 255 && heldWorld &&
            near(heldWorld->values[6], 53) && near(heldWorld->values[7], 64),
            "Hold preserves clock/lifecycle while a replacement transform reaches descendants");
    }

    void testMoveRySTxzSeekLoopAndChildRecreation()
    {
        Fixture fixture;
        DataBytes rootContents;
        append(rootContents, offset3D(1, 0, 0));
        append(rootContents, indirect(packDataId(2, 1)));
        DataBytes childContents;
        append(childContents, offset3D(0, 0, 5));
        const std::array items{
            ArchiveBuildItem{LegacyDataType::Chunky,
                sequence(0, 8, 3, 0, rootContents)},
            ArchiveBuildItem{LegacyDataType::Chunky,
                sequence(0, 20, 2, 5, childContents)}};
        auto program = makeProgram(fixture.root / "move-3d.dat", items);
        const auto rootDataId = packDataId(2, 0);
        const auto childDataId = packDataId(2, 1);
        SequenceRuntime runtime;
        SequenceCommandQueue commands(runtime);
        (void)commands.enqueue(StartSequenceCommand{program, 30});
        expect(commands.updateCycle(0).has_value(), "3D loop tree starts");
        const auto rootId = runtime.matching(rootDataId, 30).front();

        constexpr float halfPi = 1.57079632679489661923F;
        (void)commands.enqueue(makeMoveRySTxz(rootDataId, 30, halfPi, 2, 10, 20));
        expect(commands.updateCycle(0).has_value() && commands.outcomes()[0].matched == 1,
            "MoveRySTxz queues through the same historical Move command");
        auto moved = runtime.inspect(rootId);
        const auto* matrix = moved ? std::get_if<Matrix3D>(&moved->localTransform) : nullptr;
        expect(matrix && near(matrix->values[0], 0) && near(matrix->values[2], -2) &&
            near(matrix->values[8], 2) && near(matrix->values[10], 0) &&
            near(matrix->values[12], 10) && near(matrix->values[14], 20),
            "MoveRySTxz builds uniform scale, Y yaw, then X/Z translation in row-vector order");

        const auto oldNested = runtime.matching(childDataId, 5, true).front();
        (void)commands.enqueue(makeMoveTheWorks(childDataId, 5,
            SequenceTransform(translate3D(30, 0, 40)), true));
        expect(commands.updateCycle(0).has_value(),
            "MoveTheWorks reaches an exact nested target when whole-tree search is requested");
        const auto movedNested = runtime.inspect(oldNested);
        const auto* nestedLocal = movedNested ?
            std::get_if<Matrix3D>(&movedNested->localTransform) : nullptr;
        expect(nestedLocal && near(nestedLocal->values[12], 30) &&
            near(nestedLocal->values[14], 40),
            "MoveTheWorks replaces nested local transform state");

        expect(runtime.seek(rootId, 4).has_value(), "seek rebuilds the moved root's children");
        const auto afterSeek = runtime.inspect(rootId);
        const auto* retained = afterSeek ?
            std::get_if<Matrix3D>(&afterSeek->localTransform) : nullptr;
        const auto recreatedMatches = runtime.matching(childDataId, 5, true);
        const auto recreated = recreatedMatches.empty() ? std::nullopt :
            runtime.inspect(recreatedMatches.front());
        const auto* recreatedLocal = recreated ?
            std::get_if<Matrix3D>(&recreated->localTransform) : nullptr;
        expect(retained && near(retained->values[12], 10) && near(retained->values[14], 20) &&
            !runtime.inspect(oldNested),
            "seek preserves parent runtime movement and destroys the old child instance");
        expect(recreated && recreated->node != oldNested && recreatedLocal &&
            near(recreatedLocal->values[12], 0) && near(recreatedLocal->values[14], 5),
            "seek recreates child movement from immutable sequence description");

        const auto preLoopChild = recreated->node;
        expect(commands.updateCycle(8).has_value(), "natural loop executes after movement");
        const auto loopedRoot = runtime.inspect(rootId);
        const auto loopChildren = runtime.matching(childDataId, 5, true);
        const auto* loopLocal = loopedRoot ?
            std::get_if<Matrix3D>(&loopedRoot->localTransform) : nullptr;
        expect(loopedRoot && loopedRoot->clock == 0 && loopLocal &&
            near(loopLocal->values[12], 10) && !runtime.inspect(preLoopChild) &&
            loopChildren.size() == 1 && loopChildren.front() != preLoopChild,
            "natural loop retains parent movement while destroying and recreating children exactly once");

        (void)commands.enqueue(makeMoveTheWorks(rootDataId, 30,
            SequenceTransform(identity2D()), false));
        expect(commands.updateCycle(8).has_value(),
            "dimension-mismatched MoveTheWorks remains a valid historical reset");
        const auto dimensionReset = runtime.inspect(rootId);
        const auto* identity = dimensionReset ?
            std::get_if<Matrix3D>(&dimensionReset->localTransform) : nullptr;
        expect(dimensionReset && !dimensionReset->explicitlyPositioned && identity &&
            near(identity->values[0], 1) && near(identity->values[12], 0),
            "a supplied matrix of the wrong dimension resets the target to identity");
    }
}

int main()
{
    testNestedBatchingAndFifo();
    testWholeTreeTargeting();
    testCapacityNegativeNestingAndErrors();
    testMoveXYReplacementAndRecursiveWorldPropagation();
    testMoveRySTxzSeekLoopAndChildRecreation();
    std::cout << (failures ? "Sequence command tests FAILED\n" :
        "Sequence command tests passed\n");
    return failures ? 1 : 0;
}
