#pragma once

#include "RenderSlots.hpp"
#include "SequenceRenderData.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
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
    };
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
        void clear() noexcept;
        [[nodiscard]] std::size_t size() const noexcept;
        [[nodiscard]] const SequenceWorld3DObject* find(
            sequence::SequenceNodeId node) const noexcept;
        [[nodiscard]] std::vector<sequence::SequenceNodeId> order() const;
    private:
        std::map<sequence::SequenceNodeId, SequenceWorld3DObject> objects_;
        std::vector<sequence::SequenceNodeId> order_;
    };
}
