#pragma once

#include "ResourceRuntime.hpp"
#include "SequenceChildSchedule.hpp"
#include "SequenceClock.hpp"
#include "SequenceTransforms.hpp"

#include <array>
#include <memory>
#include <string>

namespace monopoly::sequence
{
    enum class RuntimeErrorCode
    {
        DataFailure, DecodeFailure, UnsupportedType, UnsupportedAttribute,
        InvalidLimits, DepthLimit, DescriptionLimit, ReferenceLimit, IndirectCycle,
        LiveNodeLimit, BirthLimit, InvalidHandle, ClockFailure, TweekerFailure,
        IdentifierExhausted
    };
    struct RuntimeError
    {
        RuntimeErrorCode code{};
        data::DataId dataId{};
        std::size_t offset{};
        std::string detail;
        std::variant<std::monostate, data::DataError, data::ChunkError,
            data::SequenceError, ClockError> cause;
    };
    struct DescriptionLimits
    {
        std::size_t maximumDepth{64}; // root counts as one; hard safety ceiling 128
        std::size_t maximumDescriptions{4096};
        std::size_t maximumReferences{65'536};
    };

    struct SequenceDescription
    {
        data::DataId dataId{};
        data::LegacySequenceRecord record;
        SequenceChildSchedule children;
        data::LegacySequenceAttributes attributes;
        // Resolved exactly as LI_SEQNCR_StartUpSequence does for 3D mesh
        // records. Empty for records which do not expose renderable content.
        std::optional<data::DataId> contentsDataId;
        // One program index for each record in the schedule, in DISK order.
        std::vector<std::size_t> childDescriptions;
    };

    // Immutable, bounded description DAG. Shared sublists are not expanded
    // exponentially; cycles are rejected by (DATA ID, chunk offset). Every
    // CNK lease needed by the supported tree is acquired before publication.
    // Currently executable: grouping/indirect, 3D mesh, 3D camera and
    // transform/FOV tweekers. Private attributes are immutable input.
    // Other decoded kinds and attributes fail explicitly; no fake renderer.
    class SequenceProgram final
    {
    public:
        [[nodiscard]] static std::expected<std::shared_ptr<const SequenceProgram>, RuntimeError>
        load(const data::DataBankRegistry& registry, data::DataId id,
            std::size_t offset = 0, DescriptionLimits limits = {});
        [[nodiscard]] static std::expected<std::shared_ptr<const SequenceProgram>, RuntimeError>
        load(std::shared_ptr<const data::ResourceSnapshot> resources, data::DataId id,
            std::size_t offset = 0, DescriptionLimits limits = {});
        [[nodiscard]] std::span<const SequenceDescription> descriptions() const noexcept;
        [[nodiscard]] std::shared_ptr<const data::ResourceSnapshot> resources() const noexcept;
    private:
        SequenceProgram() = default;
        std::vector<SequenceDescription> descriptions_;
        std::shared_ptr<const data::ResourceSnapshot> resources_;
    };

    using SequenceNodeId = std::uint64_t; // zero means no node; never reused
    enum class SequenceEventKind { Created, Updated, ReachedEnd, Rewound, Destroyed };
    struct SequenceEvent
    {
        SequenceEventKind kind{};
        SequenceNodeId node{};
        SequenceNodeId parent{};
        data::DataId dataId{};
        std::size_t offset{};
        std::uint16_t priority{};
        std::int32_t clock{};
    };
    struct SequenceMeshChoice3D
    {
        std::int16_t meshIndexA{};
        std::int16_t meshIndexB{};
        float meshProportion{};
        auto operator<=>(const SequenceMeshChoice3D&) const = default;
    };
    struct SequenceCamera3DView
    {
        SequenceNodeId node{};
        std::uint8_t label{};
        std::uint16_t priority{};
        std::int32_t clock{};
        Matrix3D worldTransform{};
        float fieldOfView{0.7853981633974483F};
        float nearPlane{1.0F};
        float farPlane{5000.0F};
    };
    struct SequenceNodeView
    {
        SequenceNodeId node{};
        SequenceNodeId parent{};
        data::DataId dataId{};
        std::size_t offset{};
        std::uint16_t priority{};
        std::int32_t clock{};
        std::int32_t endTime{};
        std::uint8_t timeMultiple{};
        bool paused{};
        std::uint8_t dimensionality{};
        std::optional<data::DataId> contentsDataId;
        bool explicitlyPositioned{};
        SequenceTransform localTransform;
        bool tweekerTransformApplied{};
        SequenceTransform tweekerTransform;
        SequenceTransform worldTransform;
        std::vector<SequenceNodeId> children; // runtime priority order
        SequenceMeshChoice3D meshChoice{};
    };

    struct SequenceMeshInstanceView
    {
        SequenceNodeId node{};
        data::DataId contentsDataId{};
        std::uint16_t priority{};
        std::int32_t clock{};
        Matrix3D worldTransform{};
        SequenceMeshChoice3D meshChoice{};
    };
    struct RuntimeLimits
    {
        std::size_t maximumLiveNodes{65'536};
        std::size_t maximumBirthsPerOperation{65'536};
    };

    // CPU forest below the historical implicit root. No SDL, UI callbacks,
    // scene or GPU objects. All mutations are on the owner thread. Children
    // are uniquely owned; handles/views never expose mutable node pointers.
    // Start is staged. An update/seek failure destroys the forest explicitly
    // instead of leaving a partially evaluated tree looking successful.
    class SequenceRuntime final
    {
    public:
        explicit SequenceRuntime(RuntimeLimits limits = {});
        ~SequenceRuntime();
        SequenceRuntime(const SequenceRuntime&) = delete;
        SequenceRuntime& operator=(const SequenceRuntime&) = delete;

        [[nodiscard]] std::expected<SequenceNodeId, RuntimeError> start(
            std::shared_ptr<const SequenceProgram> program,
            std::uint16_t priority = 0, ClockStartOptions options = {});
        [[nodiscard]] std::expected<void, RuntimeError> update(std::int32_t parentClock);
        [[nodiscard]] std::expected<void, RuntimeError> stop(SequenceNodeId node);
        void stopAll();
        [[nodiscard]] std::expected<void, RuntimeError> setPaused(SequenceNodeId node, bool paused);
        [[nodiscard]] std::expected<void, RuntimeError> seek(SequenceNodeId node, std::int32_t time);
        [[nodiscard]] std::expected<void, RuntimeError> setEndingAction(SequenceNodeId node, std::uint8_t action);

        // Historical command targeting excludes nested records (offset != 0),
        // including during whole-tree searches. Duplicate matches are legal.
        [[nodiscard]] std::vector<SequenceNodeId> matching(data::DataId id,
            std::uint16_t priority, bool wholeTree = false) const;
        [[nodiscard]] std::size_t stopMatching(data::DataId id,
            std::uint16_t priority, bool wholeTree = false);
        [[nodiscard]] std::expected<std::size_t, RuntimeError> setEndingActionMatching(
            data::DataId id, std::uint16_t priority, std::uint8_t action,
            bool wholeTree = false);
        [[nodiscard]] std::size_t moveMatching(data::DataId id,
            std::uint16_t priority, const SequenceTransform& transform,
            bool wholeTree = false);

        [[nodiscard]] std::optional<SequenceNodeView> inspect(SequenceNodeId node) const;
        // Active 3D mesh leaves in runtime traversal order. This is CPU render
        // intent only: no HMD decoding, GPU resource or render-slot ownership.
        [[nodiscard]] std::vector<SequenceMeshInstanceView> meshInstances() const;
        // Active 3D camera sequences with raw ArtLib FOV semantics. Projection
        // interpretation remains the renderer's responsibility.
        [[nodiscard]] std::vector<SequenceCamera3DView> cameraInstances() const;
        // Mirrors LE_SEQNCR_LabelArray for camera records: the most recently
        // started owner wins; deleting it clears the label without restoring
        // an older overlapping owner.
        [[nodiscard]] std::optional<SequenceCamera3DView> cameraForLabel(
            std::uint8_t label) const;
        [[nodiscard]] std::vector<SequenceNodeId> roots() const;
        [[nodiscard]] std::size_t liveNodeCount() const noexcept;
        // Diagnostic lifecycle trace for the last operation, not ArtLib label
        // notifications/callbacks. Cleared on each mutation: no growing queue.
        [[nodiscard]] std::span<const SequenceEvent> events() const noexcept;

    private:
        struct Node;
        using Nodes = std::vector<std::unique_ptr<Node>>;
        [[nodiscard]] Node* find(SequenceNodeId id) const;
        [[nodiscard]] std::expected<std::unique_ptr<Node>, RuntimeError> create(
            std::shared_ptr<const SequenceProgram> program, std::size_t description,
            Node* parent, std::uint16_t priority, ClockStartOptions options);
        void insert(Nodes& siblings, std::unique_ptr<Node> node);
        void emit(SequenceEventKind kind, const Node& node);
        void destroyChildren(Node& node);
        void destroy(std::unique_ptr<Node>& node);
        void erase(Node& node);
        void clearForest();
        void forceAncestors(Node& node);
        void forceDescendants(Node& node);
        void move(Node& node, const SequenceTransform& transform);
        [[nodiscard]] std::expected<void, RuntimeError> birthChildren(Node& node,
            std::optional<std::int32_t> previous);
        [[nodiscard]] std::expected<void, RuntimeError> rebuildChildren(Node& node);
        [[nodiscard]] std::expected<bool, RuntimeError> seekNode(Node& node, std::int32_t time);
        [[nodiscard]] std::expected<void, RuntimeError> applyTweeker(Node& node);
        [[nodiscard]] std::expected<bool, RuntimeError> updateNode(Node& node, std::int32_t parentClock);
        RuntimeLimits limits_;
        Nodes roots_;
        std::vector<SequenceEvent> events_;
        SequenceNodeId nextId_{1};
        std::size_t liveNodes_{};
        std::size_t births_{};
        std::int32_t parentClock_{};
        bool clockStarted_{};
        std::array<SequenceNodeId, 256> cameraLabelOwners_{};
    };
}
