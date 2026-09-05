#pragma once

#include "MeshRuntime.hpp"
#include "SequenceRuntime.hpp"

#include <expected>
#include <memory>
#include <vector>

namespace monopoly::sequence
{
    enum class SequenceRenderDataErrorCode { MeshResolutionFailed };

    struct SequenceRenderDataError
    {
        SequenceRenderDataErrorCode code{};
        SequenceNodeId node{};
        data::DataId contentsDataId{};
        data::MeshRuntimeError cause;
    };

    struct SequenceMeshRenderItem
    {
        SequenceNodeId node{};
        data::DataId contentsDataId{};
        std::uint16_t priority{};
        std::int32_t clock{};
        Matrix3D worldTransform{};
        std::shared_ptr<const data::MeshRuntimeAsset> asset;
        SequenceMeshChoice3D meshChoice{};
    };

    // Resolves the current CPU mesh intent transactionally. No SDL/GPU/render
    // slot object is created here; failed mesh resolution publishes nothing.
    [[nodiscard]] std::expected<std::vector<SequenceMeshRenderItem>,
        SequenceRenderDataError> collectSequenceMeshRenderData(
            const SequenceRuntime& runtime, data::MeshRuntimeCache& meshes);
}
