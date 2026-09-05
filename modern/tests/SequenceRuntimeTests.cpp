#include "SequenceRuntime.hpp"
#include "LegacyDataArchiveBuilder.hpp"

#include <algorithm>
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
    DataBytes sequence(std::int32_t start, std::int32_t end, std::uint8_t action,
        std::uint8_t priority, bool drop, const DataBytes& children = {})
    {
        DataBytes payload;
        word(payload, (static_cast<std::uint32_t>(priority) << 24U) |
            (static_cast<std::uint32_t>(start) & 0x00FF'FFFFU));
        word(payload, (4U << 24U) | (drop ? 0x4000'0000U : 0U) |
            (static_cast<std::uint32_t>(end) & 0x00FF'FFFFU));
        word(payload, action);
        append(payload, children);
        return chunk(1, payload);
    }
    DataBytes indirect(DataId target, bool absolute, std::int32_t start = 0)
    {
        DataBytes payload;
        word(payload, static_cast<std::uint32_t>(start) & 0x00FF'FFFFU);
        word(payload, 4U << 24U);
        word(payload, 1U | (absolute ? 16U : 0U));
        word(payload, target);
        return chunk(2, payload);
    }

    struct Fixture
    {
        std::filesystem::path root;
        Fixture()
        {
            root = std::filesystem::current_path() /
                ("SequenceRuntime-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
            if (!std::filesystem::create_directory(root))
                throw std::runtime_error("sequence fixture directory collision");
        }
        ~Fixture()
        {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }
    };
    std::shared_ptr<LegacyDataArchive> archive(
        const std::filesystem::path& path, std::span<const ArchiveBuildItem> items,
        DataBankRegistry& registry, std::uint16_t group = 2)
    {
        const auto written = writeLegacyDataArchive(path, items);
        expect(written.has_value(), "synthetic runtime CNK archive is written");
        if (!written) return {};
        auto mounted = registry.mount(path, group);
        expect(mounted.has_value(), "synthetic runtime CNK archive mounts");
        return mounted ? *mounted : nullptr;
    }
    std::shared_ptr<const SequenceProgram> program(
        const DataBankRegistry& registry, DataTag tag = 0,
        DescriptionLimits limits = {})
    {
        auto loaded = SequenceProgram::load(registry, packDataId(2, tag), 0, limits);
        expect(loaded.has_value(), "immutable sequence program loads");
        return loaded ? *loaded : nullptr;
    }
    std::size_t count(std::span<const SequenceEvent> events, SequenceEventKind kind)
    {
        return static_cast<std::size_t>(std::count_if(events.begin(), events.end(),
            [kind](const auto& event) { return event.kind == kind; }));
    }

    void testRecursiveLifecycleAndOrder()
    {
        Fixture fixture;
        DataBytes nested;
        append(nested, sequence(2, 0, 2, 3, true)); // grandchild, Hold
        DataBytes children;
        append(children, sequence(0, 0, 2, 5, true));
        append(children, sequence(0, 0, 2, 5, true, nested));
        append(children, sequence(5, 0, 2, 1, true));
        const std::array items{ArchiveBuildItem{LegacyDataType::Chunky,
            sequence(0, 20, 2, 9, true, children)}};
        DataBankRegistry registry;
        (void)archive(fixture.root / "tree.dat", items, registry);
        auto executable = program(registry);
        SequenceRuntime runtime;
        auto root = runtime.start(executable, 700);
        expect(root && runtime.liveNodeCount() == 1,
            "Start creates only the top-level node and preserves 16-bit priority");
        expect(runtime.inspect(*root)->priority == 700,
            "command priority is not truncated to the record's eight bits");
        expect(runtime.inspect(*root)->dimensionality == 0 &&
            !runtime.inspect(*root)->tweekerTransformApplied &&
            std::holds_alternative<std::monostate>(runtime.inspect(*root)->worldTransform),
            "unpositioned grouping root remains zero-dimensional");
        expect(runtime.update(100).has_value(), "initial recursive update succeeds");
        auto rootView = runtime.inspect(*root);
        expect(rootView && rootView->children.size() == 2 && runtime.liveNodeCount() == 3,
            "two children starting exactly at tick zero are created once");
        const auto first = runtime.inspect(rootView->children[0]);
        const auto second = runtime.inspect(rootView->children[1]);
        expect(first && second && first->offset > second->offset,
            "equal-priority siblings execute in reverse creation/disk order");
        std::vector<std::size_t> createdOffsets;
        for (const auto& event : runtime.events())
            if (event.kind == SequenceEventKind::Created && event.parent == *root)
                createdOffsets.push_back(event.offset);
        expect(createdOffsets.size() == 2 && createdOffsets[0] < createdOffsets[1],
            "creation remains in stable disk order before runtime insertion");
        expect(runtime.update(105).has_value(), "large recursive update succeeds");
        rootView = runtime.inspect(*root);
        expect(rootView && rootView->children.size() == 3 && runtime.liveNodeCount() == 5,
            "later child and nested grandchild are born during the same jump");
        const auto nestedParent = std::find_if(rootView->children.begin(), rootView->children.end(),
            [&](auto id) { return !runtime.inspect(id)->children.empty(); });
        expect(nestedParent != rootView->children.end() &&
            runtime.inspect(runtime.inspect(*nestedParent)->children.front())->clock == 3,
            "late drop-frames grandchild starts at parentClock-parentStartTime");
        const auto total = runtime.liveNodeCount();
        expect(runtime.update(105).has_value() && runtime.liveNodeCount() == total &&
            count(runtime.events(), SequenceEventKind::Created) == 0,
            "same tick cannot double-create children");

        const auto stoppedChild = rootView->children[1];
        expect(runtime.stop(stoppedChild).has_value() && !runtime.inspect(stoppedChild) &&
            runtime.inspect(*root), "explicit child stop preserves its parent and siblings");
        const auto remaining = runtime.liveNodeCount();
        expect(count(runtime.events(), SequenceEventKind::Destroyed) == total - remaining,
            "stopping a nested child destroys its own descendants first");
        expect(!runtime.stop(stoppedChild) && runtime.liveNodeCount() == remaining,
            "stale handle cannot double-destroy a node");
        runtime.stopAll();
        expect(runtime.liveNodeCount() == 0 && !runtime.inspect(*root) &&
            count(runtime.events(), SequenceEventKind::Destroyed) == remaining,
            "stopAll destroys descendants before ancestors exactly once");
    }

    void testEndCrossingHoldAndLoop()
    {
        Fixture fixture;
        DataBytes children;
        append(children, sequence(5, 3, 1, 1, true)); // wholly crossed by jump to 10
        append(children, sequence(10, 0, 2, 2, true)); // exact boundary
        const std::array stopItems{ArchiveBuildItem{LegacyDataType::Chunky,
            sequence(0, 10, 1, 0, true, children)}};
        DataBankRegistry stopRegistry;
        (void)archive(fixture.root / "stop.dat", stopItems, stopRegistry);
        SequenceRuntime stopped;
        auto stopRoot = stopped.start(program(stopRegistry), 4).value();
        (void)stopped.update(0);
        expect(stopped.update(10).has_value() && !stopped.inspect(stopRoot) &&
            stopped.liveNodeCount() == 0 &&
            count(stopped.events(), SequenceEventKind::Created) == 0,
            "parent ending exactly now dies before crossed or boundary children are born");

        DataBytes loopChildren;
        append(loopChildren, sequence(0, 0, 2, 1, false));
        append(loopChildren, sequence(4, 0, 2, 2, false));
        const std::array loopItems{ArchiveBuildItem{LegacyDataType::Chunky,
            sequence(0, 8, 3, 0, true, loopChildren)}};
        DataBankRegistry loopRegistry;
        auto mounted = archive(fixture.root / "loop.dat", loopItems, loopRegistry);
        SequenceRuntime looping;
        auto loopRoot = looping.start(program(loopRegistry), 3).value();
        (void)looping.update(0);
        const auto oldChild = looping.inspect(loopRoot)->children.front();
        (void)looping.update(4);
        const auto oldChildren = looping.inspect(loopRoot)->children;
        expect(oldChildren.size() == 2, "pre-loop runtime contains both eligible children");
        expect(looping.update(8).has_value(), "natural loop update succeeds");
        const auto newChildren = looping.inspect(loopRoot)->children;
        expect(newChildren.size() == 1 && newChildren.front() != oldChild &&
            !looping.inspect(oldChildren[0]) && !looping.inspect(oldChildren[1]),
            "loop destroys every old child and recreates only time-zero children");
        expect(count(looping.events(), SequenceEventKind::Destroyed) == 2 &&
            count(looping.events(), SequenceEventKind::Created) == 1,
            "loop emits one destruction per old node and one creation per reborn node");

        expect(looping.setEndingAction(loopRoot, 2).has_value(),
            "ending action can be changed to Hold");
        (void)looping.update(16);
        expect(looping.inspect(loopRoot)->clock == 8 &&
            looping.inspect(loopRoot)->timeMultiple == 255,
            "Hold clamps at end and switches to the source 255-tick cadence");
        const auto heldChildren = looping.inspect(loopRoot)->children;
        expect(looping.setEndingAction(loopRoot, 3).has_value(),
            "Hold can be changed back to Loop");
        (void)looping.update(16);
        expect(looping.inspect(loopRoot)->clock == 0 &&
            looping.inspect(loopRoot)->timeMultiple == 255 &&
            looping.inspect(loopRoot)->children.size() == 1 &&
            looping.inspect(loopRoot)->children.front() != heldChildren.front(),
            "forced Hold-to-Loop rebuilds children at same tick without restoring cadence");
        loopRegistry.clear(); mounted->close();
        expect(looping.update(271).has_value() && looping.inspect(loopRoot),
            "runtime continues after registry unmount because program owns CNK bytes");
    }

    void testPauseAndSeek()
    {
        Fixture fixture;
        DataBytes children;
        append(children, sequence(0, 0, 2, 1, true));
        append(children, sequence(4, 0, 2, 2, true));
        const std::array items{ArchiveBuildItem{LegacyDataType::Chunky,
            sequence(0, 12, 3, 0, true, children)}};
        DataBankRegistry registry;
        (void)archive(fixture.root / "seek.dat", items, registry);
        SequenceRuntime runtime;
        auto root = runtime.start(program(registry), 5).value();
        (void)runtime.update(0);
        expect(runtime.setPaused(root, true).has_value(), "runtime parent pauses");
        (void)runtime.update(100);
        expect(runtime.inspect(root)->clock == 0 &&
            runtime.inspect(runtime.inspect(root)->children.front())->clock == 0,
            "paused parent gates its full descendant subtree");
        expect(runtime.setPaused(root, false).has_value(), "runtime parent resumes");
        (void)runtime.update(103);
        expect(runtime.inspect(root)->clock == 0, "resume discards elapsed paused time");
        (void)runtime.update(104);
        expect(runtime.inspect(root)->clock == 4 && runtime.inspect(root)->children.size() == 2,
            "resumed cadence creates child on its exact start tick");

        const auto oldChildren = runtime.inspect(root)->children;
        expect(runtime.seek(root, 9).has_value(), "explicit loop seek after child start succeeds");
        expect(runtime.inspect(root)->clock == 9 && runtime.inspect(root)->children.size() == 2 &&
            !runtime.inspect(oldChildren[0]) && !runtime.inspect(oldChildren[1]),
            "seek destroys and recreates children rather than mutating old instances");
        const auto sibling = runtime.inspect(root)->children.front();
        const auto other = runtime.inspect(root)->children.back();
        expect(runtime.seek(sibling, -1).has_value() && !runtime.inspect(sibling) &&
            runtime.inspect(other), "negative child seek destroys only that child and preserves sibling");
        expect(runtime.seek(root, 29).has_value() && runtime.inspect(root)->clock == 5,
            "explicit loop seek uses modulo end time");

        const std::array stopItems{ArchiveBuildItem{LegacyDataType::Chunky,
            sequence(0, 8, 1, 0, true)}};
        DataBankRegistry stopRegistry;
        (void)archive(fixture.root / "seek-stop.dat", stopItems, stopRegistry, 3);
        auto stopProgram = SequenceProgram::load(stopRegistry, packDataId(3, 0)).value();
        auto stop = runtime.start(stopProgram, 8).value();
        expect(runtime.seek(stop, 8).has_value() && !runtime.inspect(stop),
            "seek to exact Stop end destroys only the selected root");
    }

    void testCommandsAndFailureLimits()
    {
        Fixture fixture;
        const std::array items{ArchiveBuildItem{LegacyDataType::Chunky,
            sequence(0, 4, 2, 0, true)}};
        DataBankRegistry registry;
        (void)archive(fixture.root / "commands.dat", items, registry);
        auto executable = program(registry);
        SequenceRuntime runtime;
        const auto first = runtime.start(executable, 600).value();
        const auto second = runtime.start(executable, 600).value();
        const auto third = runtime.start(executable, 601).value();
        expect(runtime.roots() == std::vector<SequenceNodeId>{second, first, third},
            "top-level duplicate insertion uses priority order and newest-first equality");
        expect(runtime.matching(packDataId(2, 0), 600).size() == 2,
            "command matching retains duplicate DataID/priority instances");
        expect(runtime.setEndingActionMatching(packDataId(2, 0), 600, 3, false) == 2,
            "SetEndingAction changes every matching top-level instance");
        expect(!runtime.setEndingActionMatching(packDataId(2, 0), 600, 0, false),
            "SetEndingAction command rejects suicide zero transactionally");
        expect(runtime.stopMatching(packDataId(2, 0), 600, false) == 2 &&
            !runtime.inspect(first) && !runtime.inspect(second) && runtime.inspect(third),
            "Stop removes all matching top-level duplicates but other priorities survive");
        expect(runtime.stopMatching(packDataId(2, 0), 600, false) == 0,
            "Stop accepts an absent target without inventing an error");

        DataBytes tooMany;
        append(tooMany, sequence(0, 0, 2, 1, true));
        append(tooMany, sequence(0, 0, 2, 2, true));
        const std::array limitItems{ArchiveBuildItem{LegacyDataType::Chunky,
            sequence(0, 0, 2, 0, true, tooMany)}};
        DataBankRegistry limitRegistry;
        (void)archive(fixture.root / "limits.dat", limitItems, limitRegistry, 4);
        auto limitProgram = SequenceProgram::load(limitRegistry, packDataId(4, 0)).value();
        SequenceRuntime limited({2, 2}); // root + first child; second birth fails
        const auto limitedRoot = limited.start(limitProgram).value();
        const auto failed = limited.update(0);
        expect(!failed && failed.error().code == RuntimeErrorCode::LiveNodeLimit &&
            limited.liveNodeCount() == 0 && !limited.inspect(limitedRoot),
            "partial recursive birth failure fail-closes the forest with no live handle");
        expect(count(limited.events(), SequenceEventKind::Destroyed) == 2,
            "failure cleanup destroys each constructed node exactly once");
    }

    DataBytes nested(std::size_t levels)
    {
        DataBytes result = sequence(0, 0, 2, 0, true);
        while (--levels != 0) result = sequence(0, 0, 2, 0, true, result);
        return result;
    }
    void testProgramCyclesDepthAndAttributes()
    {
        Fixture fixture;
        const std::array depthItems{ArchiveBuildItem{LegacyDataType::Chunky, nested(4)}};
        DataBankRegistry depthRegistry;
        (void)archive(fixture.root / "depth.dat", depthItems, depthRegistry);
        DescriptionLimits limits; limits.maximumDepth = 3;
        auto depth = SequenceProgram::load(depthRegistry, packDataId(2, 0), 0, limits);
        expect(!depth && depth.error().code == RuntimeErrorCode::DepthLimit,
            "description path exceeding configured depth is rejected before runtime");
        limits.maximumDepth = 4;
        expect(SequenceProgram::load(depthRegistry, packDataId(2, 0), 0, limits).has_value(),
            "description exactly at depth limit is accepted");

        const std::array cycleItems{
            ArchiveBuildItem{LegacyDataType::Chunky, indirect(packDataId(2, 1), true)},
            ArchiveBuildItem{LegacyDataType::Chunky, indirect(packDataId(2, 0), true)} };
        DataBankRegistry cycleRegistry;
        (void)archive(fixture.root / "cycle.dat", cycleItems, cycleRegistry, 5);
        // References point at group 2; mount that physical archive there too.
        auto alsoTwo = cycleRegistry.mount(fixture.root / "cycle.dat", 2);
        expect(alsoTwo.has_value(), "cycle fixture mounts at referenced group");
        auto cycle = SequenceProgram::load(cycleRegistry, packDataId(2, 0));
        expect(!cycle && cycle.error().code == RuntimeErrorCode::IndirectCycle,
            "cross-item recursive indirect cycle is rejected on active description path");

        DataBytes attribute = sequence(0, 0, 2, 0, true, chunk(40, {}));
        const std::array attributeItems{ArchiveBuildItem{LegacyDataType::Chunky, attribute}};
        DataBankRegistry attributeRegistry;
        (void)archive(fixture.root / "attribute.dat", attributeItems, attributeRegistry, 6);
        auto unsupported = SequenceProgram::load(attributeRegistry, packDataId(6, 0));
        expect(!unsupported && unsupported.error().code == RuntimeErrorCode::UnsupportedAttribute,
            "parsed but unexecuted attributes are refused instead of silently ignored");
    }

    std::vector<ArchiveBuildItem> textItems(char16_t marker)
    {
        return {
            {LegacyDataType::IndexTable, {std::byte{42}, std::byte{0}, std::byte{0},
                std::byte{0}, std::byte{1}, std::byte{0}}},
            {LegacyDataType::String, {static_cast<std::byte>(marker & 255),
                static_cast<std::byte>(marker >> 8), std::byte{0}, std::byte{0}}}
        };
    }
    bool writeResourceSet(const std::filesystem::path& root, char16_t marker,
        const DataBytes& sequenceBytes)
    {
        const std::array names{"dat_main.dat", "dat_pat.dat", "dat_bord.dat",
            "dat_brd2.dat", "dat_3d.dat", "dat_ln01.dat", "dat_lm01.dat", "dat_lk01.dat"};
        std::error_code io;
        std::filesystem::create_directories(root / "Dat_Mon", io);
        if (io) return false;
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            std::vector<ArchiveBuildItem> items;
            if (i == 0) items.push_back({LegacyDataType::Chunky, sequenceBytes});
            else if (i == 5) items = textItems(marker);
            else items.push_back({LegacyDataType::Native, {std::byte{static_cast<unsigned char>(i)}}});
            if (!writeLegacyDataArchive(root / "Dat_Mon" / names[i], items)) return false;
        }
        return true;
    }
    ResourcePaths paths(const std::filesystem::path& root)
    { return ResourcePaths::create(std::array{root}).value(); }
    void testSnapshotReplacementLifetime()
    {
        Fixture fixture;
        DataBytes children;
        append(children, sequence(8, 0, 2, 1, true));
        expect(writeResourceSet(fixture.root / "old", u'A',
            sequence(0, 12, 3, 0, true, children)), "old eight-bank resource set is written");
        expect(writeResourceSet(fixture.root / "new", u'B',
            sequence(0, 4, 1, 0, true)), "replacement resource set is written");
        ResourceRuntime resources;
        expect(resources.initialize(paths(fixture.root / "old")).has_value(),
            "old resource snapshot initializes");
        auto oldSnapshot = resources.snapshot();
        auto executable = SequenceProgram::load(oldSnapshot, packDataId(2, 0)).value();
        SequenceRuntime runtime;
        auto root = runtime.start(executable, 1).value();
        (void)runtime.update(0);
        expect(resources.initialize(paths(fixture.root / "new")).has_value() &&
            resources.snapshot() != oldSnapshot, "resource runtime publishes replacement snapshot");
        oldSnapshot.reset();
        expect(runtime.update(8).has_value() && runtime.inspect(root)->children.size() == 1 &&
            executable->resources(),
            "program keeps old snapshot and births future child after replacement");
        (void)runtime.update(12);
        expect(runtime.inspect(root) && runtime.inspect(root)->children.empty(),
            "loop still rebuilds from old owned bytes after snapshot replacement");
        resources.shutdown();
        expect(runtime.update(20).has_value() && runtime.inspect(root),
            "runtime remains valid after replacement service shutdown");
    }
}

int main()
{
    try
    {
        testRecursiveLifecycleAndOrder();
        testEndCrossingHoldAndLoop();
        testPauseAndSeek();
        testCommandsAndFailureLimits();
        testProgramCyclesDepthAndAttributes();
        testSnapshotReplacementLifetime();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[FAIL] unexpected test exception: " << exception.what() << '\n';
        ++failures;
    }
    std::cout << "Sequence runtime failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
