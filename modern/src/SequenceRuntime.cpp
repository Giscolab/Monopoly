#include "SequenceRuntime.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <utility>

namespace monopoly::sequence
{
    namespace
    {
        RuntimeError error(RuntimeErrorCode code, data::DataId id,
            std::size_t offset, std::string detail)
        { return {code, id, offset, std::move(detail), {}}; }

        template<class E>
        RuntimeError caused(RuntimeErrorCode code, data::DataId id,
            std::size_t offset, E cause)
        {
            auto result = error(code, id, offset, "sequence dependency failed");
            result.cause = std::move(cause);
            return result;
        }

        SequenceTransform applyTweekerBeforeLocal(const SequenceTransform& tweeker,
            bool applied, const SequenceTransform& local, std::uint8_t dimensionality)
        {
            if (!applied) return local;
            if (dimensionality == 2)
                return multiply(std::get<Matrix2D>(tweeker), std::get<Matrix2D>(local));
            if (dimensionality == 3)
                return multiply(std::get<Matrix3D>(tweeker), std::get<Matrix3D>(local));
            return local;
        }
        SequenceMeshChoice3D initialMeshChoice(
            const data::LegacySequenceAttributes& attributes,
            std::uint8_t dimensionality) noexcept
        {
            SequenceMeshChoice3D result{};
            if (dimensionality != 3) return result;
            for (const auto& attribute : attributes.values)
                if (const auto* choice =
                    std::get_if<data::Sequence3DMeshChoiceAttribute>(&attribute))
                    result = {choice->meshIndexA, choice->meshIndexB,
                        choice->meshProportion};
            return result;
        }

        std::expected<std::optional<SequenceMeshChoice3D>, TweekerTransformError>
        evaluateTweekerMeshChoice(const data::LegacySequenceAttributes& attributes,
            std::uint8_t interpolationType, std::int32_t clock,
            std::int32_t endTime, std::uint8_t parentDimensionality) noexcept
        {
            if (interpolationType == 0) return std::nullopt;
            const data::Sequence3DMeshChoiceAttribute* first{};
            const data::Sequence3DMeshChoiceAttribute* second{};
            for (const auto& attribute : attributes.values)
                if (const auto* choice =
                    std::get_if<data::Sequence3DMeshChoiceAttribute>(&attribute))
                {
                    if (!first) first = choice;
                    else { second = choice; break; }
                }
            if (!first) return std::nullopt;
            if (parentDimensionality != 3)
                return std::unexpected(TweekerTransformError::DimensionalityMismatch);
            SequenceMeshChoice3D result{
                first->meshIndexA, first->meshIndexB, first->meshProportion};
            if (interpolationType == 2 && second && endTime < 1'234'567'890)
            {
                const float proportion = static_cast<float>(clock) /
                    static_cast<float>(endTime);
                result.meshProportion = first->meshProportion + proportion *
                    (second->meshProportion - first->meshProportion);
            }
            return result;
        }
    }

    std::expected<std::shared_ptr<const SequenceProgram>, RuntimeError>
    SequenceProgram::load(const data::DataBankRegistry& registry, data::DataId id,
        std::size_t offset, DescriptionLimits limits)
    {
        if (limits.maximumDepth == 0 || limits.maximumDepth > 128 ||
            limits.maximumDescriptions == 0)
            return std::unexpected(error(RuntimeErrorCode::InvalidLimits, id, offset,
                "description depth must be 1..128 and node budget nonzero"));

        // L_Seqncr.cpp:3637-3663, 4260-4275. LE_SEQNCR_Start accepts a raw
        // MESHX/HMD DataID by synthesizing an infinite 3D mesh sequence with
        // the ArtLib basic cadence (60 Hz), StayAtEnd, and modelDataID=DataID.
        // This is the path used by UDBoard for CurrentBoard.
        auto metadata = registry.metadata(id);
        if (!metadata)
            return std::unexpected(caused(RuntimeErrorCode::DataFailure,
                id, offset, metadata.error()));
        if (metadata->type == data::LegacyDataType::Hmd)
        {
            if (offset != 0)
                return std::unexpected(error(RuntimeErrorCode::DecodeFailure,
                    id, offset, "raw HMD sequence must start at offset zero"));
            auto program = std::shared_ptr<SequenceProgram>(new SequenceProgram);
            data::LegacySequenceHeader header{};
            header.timeMultiple = 60;
            header.endingAction = 2; // LE_SEQNCR_EndingActionStayAtEnd.
            data::LegacySequenceRecord record{
                data::ChunkInfo{9, 0, 0, 0, 0}, header,
                data::SequenceMeshData{id}, 0};
            auto children = SequenceChildSchedule::read({}, id,
                limits.maximumReferences);
            if (!children)
                return std::unexpected(std::visit([&](const auto& cause) {
                    return caused(RuntimeErrorCode::DecodeFailure, id, offset, cause);
                }, children.error()));
            program->descriptions_.push_back({id, std::move(record),
                std::move(*children), {}, id, {}});
            return std::shared_ptr<const SequenceProgram>(std::move(program));
        }

        auto program = std::shared_ptr<SequenceProgram>(new SequenceProgram);
        struct Entry { std::size_t index; bool active; std::size_t height; };
        std::map<std::pair<data::DataId, std::size_t>, Entry> visited;
        std::size_t references{};
        std::function<std::expected<std::size_t, RuntimeError>(data::DataId, std::size_t, std::size_t)> visit;
        visit = [&](data::DataId currentId, std::size_t currentOffset,
            std::size_t depth) -> std::expected<std::size_t, RuntimeError>
        {
            if (depth > limits.maximumDepth)
                return std::unexpected(error(RuntimeErrorCode::DepthLimit, currentId,
                    currentOffset, "description path exceeds configured depth"));
            const auto key = std::pair{currentId, currentOffset};
            if (const auto found = visited.find(key); found != visited.end())
            {
                if (found->second.active)
                    return std::unexpected(error(RuntimeErrorCode::IndirectCycle,
                        currentId, currentOffset, "recursive sequence description reference"));
                if (found->second.height > limits.maximumDepth - depth + 1)
                    return std::unexpected(error(RuntimeErrorCode::DepthLimit, currentId,
                        currentOffset, "shared description exceeds depth on this path"));
                return found->second.index;
            }
            if (program->descriptions_.size() >= limits.maximumDescriptions)
                return std::unexpected(error(RuntimeErrorCode::DescriptionLimit,
                    currentId, currentOffset, "description count budget exceeded"));
            auto reader = data::openLegacyChunkReader(registry, currentId);
            if (!reader) return std::unexpected(caused(RuntimeErrorCode::DataFailure,
                currentId, currentOffset, reader.error()));
            const auto positioned = reader->seek(currentOffset);
            if (!positioned) return std::unexpected(caused(RuntimeErrorCode::DecodeFailure,
                currentId, currentOffset, positioned.error()));
            auto record = data::readLegacySequenceRecord(*reader);
            if (!record) return std::unexpected(caused(RuntimeErrorCode::DecodeFailure,
                currentId, currentOffset, record.error()));
            if (record->chunk.id != 1 && record->chunk.id != 2 &&
                record->chunk.id != 9 && record->chunk.id != 10)
                return std::unexpected(error(RuntimeErrorCode::UnsupportedType,
                    currentId, currentOffset,
                    "runtime currently executes grouping, indirect, 3D mesh and tweeker records only"));
            auto attributes = data::readLegacySequenceAttributes(*reader);
            if (!attributes) return std::unexpected(caused(RuntimeErrorCode::DecodeFailure,
                currentId, currentOffset, attributes.error()));
            for (const auto& attribute : attributes->values)
                if (const auto* unsupported =
                    std::get_if<data::SequenceUnsupportedAttribute>(&attribute))
                    return std::unexpected(error(RuntimeErrorCode::UnsupportedAttribute,
                        currentId, unsupported->chunk.headerOffset,
                        "sequence attribute is decoded but its effect is not executed"));
            auto schedule = openSequenceChildSchedule(registry, currentId, currentOffset,
                limits.maximumReferences - references);
            if (!schedule)
                return std::unexpected(std::visit([&](const auto& cause) {
                    return caused(RuntimeErrorCode::DecodeFailure, currentId, currentOffset, cause);
                }, schedule.error()));
            if (schedule->records().size() > limits.maximumReferences - references)
                return std::unexpected(error(RuntimeErrorCode::ReferenceLimit,
                    currentId, currentOffset, "description reference budget exceeded"));
            references += schedule->records().size();
            std::optional<data::DataId> contentsDataId;
            if (const auto* mesh = std::get_if<data::SequenceMeshData>(&record->data))
                contentsDataId = data::resolveSequenceDataId(record->header,
                    mesh->modelDataId, currentId);
            const auto index = program->descriptions_.size();
            program->descriptions_.push_back({currentId, *record, *schedule,
                std::move(*attributes), contentsDataId, {}});
            visited.emplace(key, Entry{index, true, 1});
            std::size_t height = 1;
            for (const auto& child : schedule->records())
            {
                auto target = visit(schedule->containingDataId(), child.chunk.headerOffset, depth + 1);
                if (!target) return std::unexpected(target.error());
                program->descriptions_[index].childDescriptions.push_back(*target);
                const auto childHeight = visited.at(
                    {schedule->containingDataId(), child.chunk.headerOffset}).height;
                height = std::max(height, childHeight + 1);
            }
            visited.at(key) = {index, false, height};
            return index;
        };
        const auto root = visit(id, offset, 1);
        if (!root) return std::unexpected(root.error());
        return std::shared_ptr<const SequenceProgram>(std::move(program));
    }

    std::expected<std::shared_ptr<const SequenceProgram>, RuntimeError>
    SequenceProgram::load(std::shared_ptr<const data::ResourceSnapshot> resources,
        data::DataId id, std::size_t offset, DescriptionLimits limits)
    {
        if (!resources) return std::unexpected(error(RuntimeErrorCode::DataFailure,
            id, offset, "resource snapshot is null"));
        auto result = load(resources->banks(), id, offset, limits);
        if (!result) return result;
        // Created privately above; no mutable alias is exposed to callers.
        std::const_pointer_cast<SequenceProgram>(*result)->resources_ = std::move(resources);
        return result;
    }
    std::span<const SequenceDescription> SequenceProgram::descriptions() const noexcept
    { return descriptions_; }
    std::shared_ptr<const data::ResourceSnapshot> SequenceProgram::resources() const noexcept
    { return resources_; }

    struct SequenceRuntime::Node
    {
        SequenceNodeId id;
        Node* parent;
        std::shared_ptr<const SequenceProgram> program;
        std::size_t description;
        std::uint16_t priority;
        SequenceClock clock;
        SequenceChildSchedule schedule;
        Nodes children;
        bool reevaluate{true};
        std::uint8_t dimensionality{};
        bool explicitlyPositioned{};
        SequenceTransform localTransform;
        bool tweekerTransformApplied{};
        SequenceTransform tweekerTransform;
        SequenceTransform worldTransform;
        SequenceMeshChoice3D meshChoice{};
        const SequenceDescription& definition() const
        { return program->descriptions()[description]; }
    };

    SequenceRuntime::SequenceRuntime(RuntimeLimits limits) : limits_(limits) {}
    SequenceRuntime::~SequenceRuntime() = default;
    std::size_t SequenceRuntime::liveNodeCount() const noexcept { return liveNodes_; }
    std::span<const SequenceEvent> SequenceRuntime::events() const noexcept { return events_; }
    void SequenceRuntime::emit(SequenceEventKind kind, const Node& node)
    {
        const auto& def = node.definition();
        events_.push_back({kind, node.id, node.parent ? node.parent->id : 0,
            def.dataId, def.record.chunk.headerOffset, node.priority, node.clock.clock()});
    }
    void SequenceRuntime::insert(Nodes& siblings, std::unique_ptr<Node> node)
    {
        // InsertRuntimeChild: before first GREATER OR EQUAL priority.
        const auto position = std::lower_bound(siblings.begin(), siblings.end(), node->priority,
            [](const auto& child, std::uint16_t priority) { return child->priority < priority; });
        siblings.insert(position, std::move(node));
    }
    std::expected<std::unique_ptr<SequenceRuntime::Node>, RuntimeError>
    SequenceRuntime::create(std::shared_ptr<const SequenceProgram> program,
        std::size_t description, Node* parent, std::uint16_t priority, ClockStartOptions options)
    {
        const auto& def = program->descriptions()[description];
        const auto id = def.dataId;
        const auto offset = def.record.chunk.headerOffset;
        if (liveNodes_ >= limits_.maximumLiveNodes)
            return std::unexpected(error(RuntimeErrorCode::LiveNodeLimit, id, offset, "live node budget exceeded"));
        if (births_ >= limits_.maximumBirthsPerOperation)
            return std::unexpected(error(RuntimeErrorCode::BirthLimit, id, offset, "birth budget exceeded"));
        if (nextId_ == std::numeric_limits<SequenceNodeId>::max())
            return std::unexpected(error(RuntimeErrorCode::IdentifierExhausted, id, offset, "node identifiers exhausted"));
        if (parent) options.parentClockAtBirth = parent->clock.clock();
        else options.parentClockAtBirth.reset();
        auto clock = SequenceClock::start(def.record, options);
        if (!clock) return std::unexpected(caused(RuntimeErrorCode::ClockFailure, id, offset, clock.error()));
        const auto initial = initialSequenceTransform(def.record, def.attributes,
            parent ? parent->dimensionality : 0);
        const auto tweeker = initial.dimensionality == 2 ? SequenceTransform(identity2D()) :
            initial.dimensionality == 3 ? SequenceTransform(identity3D()) :
            SequenceTransform(std::monostate{});
        const auto world = composeSequenceWorld(initial.local, initial.dimensionality,
            parent ? parent->worldTransform : SequenceTransform(std::monostate{}),
            parent ? parent->dimensionality : 0);
        auto node = std::make_unique<Node>(Node{nextId_++, parent, std::move(program),
            description, priority, *clock, def.children, {}, true,
            initial.dimensionality, initial.explicitlyPositioned,
            initial.local, false, tweeker, world});
        node->meshChoice = initialMeshChoice(def.attributes, initial.dimensionality);
        ++liveNodes_; ++births_;
        emit(SequenceEventKind::Created, *node);
        return node;
    }
    std::expected<SequenceNodeId, RuntimeError> SequenceRuntime::start(
        std::shared_ptr<const SequenceProgram> program, std::uint16_t priority, ClockStartOptions options)
    {
        events_.clear(); births_ = 0;
        if (!program || program->descriptions().empty())
            return std::unexpected(error(RuntimeErrorCode::DataFailure, 0, 0, "no sequence program"));
        auto node = create(std::move(program), 0, nullptr, priority, options);
        if (!node) return std::unexpected(node.error());
        const auto id = (*node)->id;
        insert(roots_, std::move(*node));
        return id;
    }
    void SequenceRuntime::destroyChildren(Node& node)
    {
        for (auto& child : node.children) destroy(child);
        node.children.clear();
    }
    void SequenceRuntime::destroy(std::unique_ptr<Node>& node)
    {
        destroyChildren(*node);
        emit(SequenceEventKind::Destroyed, *node);
        --liveNodes_;
        node.reset();
    }
    void SequenceRuntime::clearForest()
    {
        for (auto& node : roots_) destroy(node);
        roots_.clear();
    }
    void SequenceRuntime::stopAll() { events_.clear(); clearForest(); }
    void SequenceRuntime::erase(Node& node)
    {
        auto& siblings = node.parent ? node.parent->children : roots_;
        const auto position = std::find_if(siblings.begin(), siblings.end(),
            [&](const auto& current) { return current.get() == &node; });
        destroy(*position);
        siblings.erase(position);
    }
    SequenceRuntime::Node* SequenceRuntime::find(SequenceNodeId id) const
    {
        const auto visit = [&](const auto& self, const Nodes& nodes) -> Node* {
            for (const auto& node : nodes)
            {
                if (node->id == id) return node.get();
                if (auto* child = self(self, node->children)) return child;
            }
            return nullptr;
        };
        return visit(visit, roots_);
    }
    void SequenceRuntime::forceAncestors(Node& node)
    { for (auto* current = &node; current; current = current->parent) current->reevaluate = true; }
    void SequenceRuntime::forceDescendants(Node& node)
    {
        node.reevaluate = true;
        for (auto& child : node.children) forceDescendants(*child);
    }
    void SequenceRuntime::move(Node& node, const SequenceTransform& transform)
    {
        bool changed = true;
        if (node.dimensionality == 2)
        {
            if (const auto* matrix = std::get_if<Matrix2D>(&transform))
            {
                const auto& current = std::get<Matrix2D>(node.localTransform);
                changed = std::memcmp(current.values.data(), matrix->values.data(),
                    sizeof(current.values)) != 0;
                if (changed)
                {
                    node.localTransform = *matrix;
                    node.explicitlyPositioned = true;
                }
            }
            else
            {
                node.localTransform = identity2D();
                node.explicitlyPositioned = false;
            }
        }
        else if (node.dimensionality == 3)
        {
            if (const auto* matrix = std::get_if<Matrix3D>(&transform))
            {
                const auto& current = std::get<Matrix3D>(node.localTransform);
                changed = std::memcmp(current.values.data(), matrix->values.data(),
                    sizeof(current.values)) != 0;
                if (changed)
                {
                    node.localTransform = *matrix;
                    node.explicitlyPositioned = true;
                }
            }
            else
            {
                node.localTransform = identity3D();
                node.explicitlyPositioned = false;
            }
        }
        if (!changed) return;

        // MarkAsNeedingPositionRecalc marks this sequence and its ancestors;
        // descendants are then reevaluated by the recursive update. Preserve
        // that observable effect even when a descendant cadence would gate it.
        forceAncestors(node);
        forceDescendants(node);
    }
    std::expected<void, RuntimeError> SequenceRuntime::stop(SequenceNodeId id)
    {
        events_.clear();
        auto* node = find(id);
        if (!node) return std::unexpected(error(RuntimeErrorCode::InvalidHandle, 0, 0, "node no longer exists"));
        erase(*node);
        return {};
    }
    std::expected<void, RuntimeError> SequenceRuntime::setPaused(SequenceNodeId id, bool paused)
    {
        events_.clear();
        auto* node = find(id);
        if (!node) return std::unexpected(error(RuntimeErrorCode::InvalidHandle, 0, 0, "node no longer exists"));
        auto result = node->clock.setPaused(paused, node->parent ? node->parent->clock.clock() : parentClock_);
        if (!result) return std::unexpected(caused(RuntimeErrorCode::ClockFailure,
            node->definition().dataId, node->definition().record.chunk.headerOffset, result.error()));
        return {};
    }
    std::expected<void, RuntimeError> SequenceRuntime::setEndingAction(SequenceNodeId id, std::uint8_t action)
    {
        events_.clear();
        auto* node = find(id);
        if (!node) return std::unexpected(error(RuntimeErrorCode::InvalidHandle, 0, 0, "node no longer exists"));
        const auto result = node->clock.setEndingAction(action);
        if (!result) return std::unexpected(caused(RuntimeErrorCode::ClockFailure,
            node->definition().dataId, node->definition().record.chunk.headerOffset, result.error()));
        forceAncestors(*node);
        return {};
    }
    std::expected<void, RuntimeError> SequenceRuntime::birthChildren(Node& node,
        std::optional<std::int32_t> previous)
    {
        const auto selected = node.schedule.select(previous, node.clock.clock());
        if (!selected) return std::unexpected(caused(RuntimeErrorCode::ClockFailure,
            node.definition().dataId, node.definition().record.chunk.headerOffset, selected.error()));
        for (const auto index : *selected)
        {
            const auto description = node.definition().childDescriptions[index];
            const auto priority = node.program->descriptions()[description].record.header.priority;
            auto child = create(node.program, description, &node, priority, {});
            if (!child) return std::unexpected(child.error());
            insert(node.children, std::move(*child));
        }
        return {};
    }
    std::expected<void, RuntimeError> SequenceRuntime::rebuildChildren(Node& node)
    {
        destroyChildren(node);
        node.schedule.rewind();
        auto born = birthChildren(node, std::nullopt);
        if (!born) return born;
        for (auto iterator = node.children.begin(); iterator != node.children.end();)
        {
            auto& child = **iterator;
            const auto time = static_cast<std::int64_t>(node.clock.clock()) -
                child.definition().record.header.parentStartTime;
            if (time < std::numeric_limits<std::int32_t>::min() || time > std::numeric_limits<std::int32_t>::max())
                return std::unexpected(caused(RuntimeErrorCode::ClockFailure,
                    child.definition().dataId, child.definition().record.chunk.headerOffset, ClockError::ClockOverflow));
            const auto alive = seekNode(child, static_cast<std::int32_t>(time));
            if (!alive) return std::unexpected(alive.error());
            if (!*alive) { destroy(*iterator); iterator = node.children.erase(iterator); }
            else ++iterator;
        }
        return {};
    }
    std::expected<bool, RuntimeError> SequenceRuntime::seekNode(Node& node, std::int32_t time)
    {
        const auto result = node.clock.seek(time, node.parent ? node.parent->clock.clock() : parentClock_);
        if (result.stopped) return false;
        node.reevaluate = true;
        emit(SequenceEventKind::Rewound, node);
        const auto rebuilt = rebuildChildren(node);
        if (!rebuilt) return std::unexpected(rebuilt.error());
        return true;
    }
    std::expected<void, RuntimeError> SequenceRuntime::seek(SequenceNodeId id, std::int32_t time)
    {
        events_.clear(); births_ = 0;
        auto* node = find(id);
        if (!node) return std::unexpected(error(RuntimeErrorCode::InvalidHandle, 0, 0, "node no longer exists"));
        forceAncestors(*node);
        const auto alive = seekNode(*node, time);
        if (!alive) { clearForest(); return std::unexpected(alive.error()); }
        if (!*alive) erase(*node);
        return {};
    }
    std::expected<bool, RuntimeError> SequenceRuntime::updateNode(Node& node, std::int32_t parentClock)
    {
        const auto tick = node.clock.update(parentClock, node.reevaluate);
        if (!tick) return std::unexpected(caused(RuntimeErrorCode::ClockFailure,
            node.definition().dataId, node.definition().record.chunk.headerOffset, tick.error()));
        if (!tick->updated) return true; // gated parent gates descendants too
        emit(SequenceEventKind::Updated, node);
        if (tick->hitEnd) emit(SequenceEventKind::ReachedEnd, node);
        if (tick->stopped) return false; // children do not get a final tick
        const auto children = tick->restartChildren ? rebuildChildren(node) : birthChildren(node, tick->previousClock);
        if (!children) return std::unexpected(children.error());
        if (node.definition().record.chunk.id == 10)
        {
            const auto applied = applyTweeker(node);
            if (!applied) return std::unexpected(applied.error());
        }

        // Tweeker children run before their parent's position calculation.
        for (auto iterator = node.children.begin(); iterator != node.children.end();)
        {
            if ((*iterator)->definition().record.chunk.id != 10) { ++iterator; continue; }
            const auto alive = updateNode(**iterator, node.clock.clock());
            if (!alive) return std::unexpected(alive.error());
            if (!*alive) { destroy(*iterator); iterator = node.children.erase(iterator); }
            else ++iterator;
        }
        const auto effectiveLocal = applyTweekerBeforeLocal(node.tweekerTransform,
            node.tweekerTransformApplied, node.localTransform, node.dimensionality);
        node.worldTransform = composeSequenceWorld(effectiveLocal,
            node.dimensionality,
            node.parent ? node.parent->worldTransform : SequenceTransform(std::monostate{}),
            node.parent ? node.parent->dimensionality : 0);
        for (auto iterator = node.children.begin(); iterator != node.children.end();)
        {
            if ((*iterator)->definition().record.chunk.id == 10) { ++iterator; continue; }
            const auto alive = updateNode(**iterator, node.clock.clock());
            if (!alive) return std::unexpected(alive.error());
            if (!*alive) { destroy(*iterator); iterator = node.children.erase(iterator); }
            else ++iterator;
        }
        node.reevaluate = false;
        return true;
    }
    std::expected<void, RuntimeError> SequenceRuntime::applyTweeker(Node& node)
    {
        if (!node.parent) return std::unexpected(error(RuntimeErrorCode::TweekerFailure,
            node.definition().dataId, node.definition().record.chunk.headerOffset,
            "a tweeker must be a child of the sequence it modifies"));
        const auto& tweeker = std::get<data::SequenceTweekerData>(node.definition().record.data);
        const auto evaluated = evaluateTweekerTransform(node.definition().attributes,
            tweeker.interpolationType, node.clock.clock(), node.clock.endTime(),
            node.parent->dimensionality);
        if (!evaluated)
            return std::unexpected(error(RuntimeErrorCode::TweekerFailure,
                node.definition().dataId, node.definition().record.chunk.headerOffset,
                evaluated.error() == TweekerTransformError::InvalidInterpolation ?
                    "tweeker interpolation type is not implemented" :
                    "tweeker transform dimensionality does not match its parent"));

        const auto meshChoice = evaluateTweekerMeshChoice(node.definition().attributes,
            tweeker.interpolationType, node.clock.clock(), node.clock.endTime(),
            node.parent->dimensionality);
        if (!meshChoice)
            return std::unexpected(error(RuntimeErrorCode::TweekerFailure,
                node.definition().dataId, node.definition().record.chunk.headerOffset,
                "3D mesh-choice tweeker dimensionality does not match its parent"));
        if (evaluated->changed)
        {
            node.parent->tweekerTransformApplied = !evaluated->identity;
            node.parent->tweekerTransform = evaluated->transform;
        }
        if (*meshChoice)
            node.parent->meshChoice = **meshChoice;
        return {};
    }
    std::expected<void, RuntimeError> SequenceRuntime::update(std::int32_t parentClock)
    {
        events_.clear(); births_ = 0;
        if (clockStarted_ && parentClock < parentClock_)
            return std::unexpected(caused(RuntimeErrorCode::ClockFailure, 0, 0, ClockError::ParentClockWentBackwards));
        parentClock_ = parentClock; clockStarted_ = true;
        for (auto iterator = roots_.begin(); iterator != roots_.end();)
        {
            const auto alive = updateNode(**iterator, parentClock);
            if (!alive) { clearForest(); return std::unexpected(alive.error()); }
            if (!*alive) { destroy(*iterator); iterator = roots_.erase(iterator); }
            else ++iterator;
        }
        return {};
    }
    std::vector<SequenceNodeId> SequenceRuntime::matching(data::DataId id,
        std::uint16_t priority, bool wholeTree) const
    {
        std::vector<SequenceNodeId> matches;
        const auto visit = [&](const auto& self, const Nodes& nodes) -> void {
            for (const auto& node : nodes)
            {
                if (node->definition().dataId == id && node->priority == priority &&
                    node->definition().record.chunk.headerOffset == 0) matches.push_back(node->id);
                if (wholeTree) self(self, node->children);
            }
        };
        visit(visit, roots_);
        return matches;
    }
    std::size_t SequenceRuntime::stopMatching(data::DataId id,
        std::uint16_t priority, bool wholeTree)
    {
        events_.clear();
        const auto matches = matching(id, priority, wholeTree);
        std::size_t stopped{};
        for (const auto match : matches)
        {
            // A matching ancestor deletes matching descendants with it.
            // FindNextSequence likewise resumes from the surviving tree.
            if (auto* node = find(match))
            {
                erase(*node);
                ++stopped;
            }
        }
        return stopped;
    }
    std::expected<std::size_t, RuntimeError> SequenceRuntime::setEndingActionMatching(
        data::DataId id, std::uint16_t priority, std::uint8_t action, bool wholeTree)
    {
        events_.clear();
        if (action == 0 || action > 3)
            return std::unexpected(caused(RuntimeErrorCode::ClockFailure, id, 0, ClockError::InvalidEndingAction));
        const auto matches = matching(id, priority, wholeTree);
        for (const auto match : matches)
        {
            auto* node = find(match);
            (void)node->clock.setEndingAction(action);
            forceAncestors(*node);
        }
        return matches.size();
    }
    std::size_t SequenceRuntime::moveMatching(data::DataId id,
        std::uint16_t priority, const SequenceTransform& transform, bool wholeTree)
    {
        events_.clear();
        const auto matches = matching(id, priority, wholeTree);
        for (const auto match : matches)
            if (auto* node = find(match)) move(*node, transform);
        // FindNextSequence counts a target even when MoveSequence's exact
        // matrix comparison makes the individual mutation a no-op.
        return matches.size();
    }
    std::optional<SequenceNodeView> SequenceRuntime::inspect(SequenceNodeId id) const
    {
        const auto* node = find(id);
        if (!node) return std::nullopt;
        SequenceNodeView view{node->id, node->parent ? node->parent->id : 0,
            node->definition().dataId, node->definition().record.chunk.headerOffset, node->priority,
            node->clock.clock(), node->clock.endTime(), node->clock.timeMultiple(), node->clock.paused(),
            node->dimensionality, node->definition().contentsDataId, node->explicitlyPositioned,
            node->localTransform, node->tweekerTransformApplied,
            node->tweekerTransform, node->worldTransform, {}};
        view.meshChoice = node->meshChoice;
        for (const auto& child : node->children) view.children.push_back(child->id);
        return view;
    }
    std::vector<SequenceMeshInstanceView> SequenceRuntime::meshInstances() const
    {
        std::vector<SequenceMeshInstanceView> result;
        const auto visit = [&](const auto& self, const Nodes& nodes) -> void {
            for (const auto& node : nodes)
            {
                const auto& definition = node->definition();
                if (definition.record.chunk.id == 9 && definition.contentsDataId &&
                    node->dimensionality == 3 &&
                    std::holds_alternative<Matrix3D>(node->worldTransform))
                {
                    result.push_back({node->id, *definition.contentsDataId,
                        node->priority, node->clock.clock(),
                        std::get<Matrix3D>(node->worldTransform), node->meshChoice});
                }
                self(self, node->children);
            }
        };
        visit(visit, roots_);
        return result;
    }

    std::vector<SequenceNodeId> SequenceRuntime::roots() const
    {
        std::vector<SequenceNodeId> result;
        for (const auto& root : roots_) result.push_back(root->id);
        return result;
    }
}
