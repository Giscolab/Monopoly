#pragma once

#include "RenderSlots.hpp"
#include "SequenceCommands.hpp"
#include "SequenceRenderData.hpp"
#include "World3DProjection.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace monopoly::engine
{
    enum class SequenceWorld3DSlotErrorCode { DuplicateNode };

    struct SequenceWorld3DSlotError
    {
        SequenceWorld3DSlotErrorCode code{};
        sequence::SequenceNodeId node{};
    };

    struct SequenceWorld3DObject
    {
        sequence::SequenceNodeId node{};
        data::DataId contentsDataId{};
        std::uint16_t priority{};
        std::int32_t clock{};
        sequence::Matrix3D worldTransform{};
        std::shared_ptr<const data::MeshRuntimeAsset> asset;
        std::optional<World3DRect> screenBounds;
        std::shared_ptr<const data::MeshRenderData> renderData;
    };

    // Resolves the historical SetCamera render-slot state. A labelled
    // camera with no current label owner intentionally returns nullopt so the
    // caller can preserve the previously installed camera, matching ArtLib.
    [[nodiscard]] World3DCamera world3DCameraFromSequence(
        const sequence::SequenceCamera3DView& camera) noexcept;
    [[nodiscard]] std::optional<World3DCamera> resolveWorld3DCamera(
        const sequence::SetCameraCommand& command,
        const sequence::SequenceRuntime& runtime) noexcept;

    struct SequenceWorld3DSyncStats
    {
        std::size_t started{};
        std::size_t moved{};
        std::size_t stopped{};
        std::size_t unchanged{};
    };

    // CPU-side ownership/lifecycle boundary for historical render slot 1.
    // SequenceStartUp => create entry; SequenceMoved => update matrix/state;
    // SequenceShutDown => remove entry. GPU resources remain outside this type.
    class SequenceWorld3DSlot final
    {
    public:
        [[nodiscard]] static constexpr RenderSlot slot() noexcept
        { return RenderSlot::World3D; }

        [[nodiscard]] std::expected<SequenceWorld3DSyncStats,
            SequenceWorld3DSlotError> sync(
                const std::vector<sequence::SequenceMeshRenderItem>& items);

        [[nodiscard]] std::expected<std::size_t, World3DProjectionError>
            configureView(World3DRect viewport, World3DCamera camera = {});
        void clearView() noexcept;

        void clear() noexcept;
        [[nodiscard]] std::size_t size() const noexcept;
        [[nodiscard]] const SequenceWorld3DObject* find(
            sequence::SequenceNodeId node) const noexcept;
        [[nodiscard]] std::vector<sequence::SequenceNodeId> order() const;
        [[nodiscard]] std::vector<sequence::SequenceNodeId> visibleOrder() const;
        [[nodiscard]] const std::optional<World3DProjectionState>&
            view() const noexcept;

    private:
        void refreshBounds(SequenceWorld3DObject& object) noexcept;
        void refreshAllBounds() noexcept;

        std::map<sequence::SequenceNodeId, SequenceWorld3DObject> objects_;
        std::vector<sequence::SequenceNodeId> order_;
        std::optional<World3DProjectionState> view_;
    };
}
