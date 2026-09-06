#pragma once

#include "LegacyBitmap.hpp"
#include "ResourceRuntime.hpp"
#include "SequenceRuntime.hpp"

#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace monopoly::sequence
{
    enum class SequenceBitmapRenderDataErrorCode
    {
        MissingResources,
        MetadataFailed,
        TypeMismatch,
        LoadFailed,
        InvalidBitmap
    };

    struct SequenceBitmapRenderDataError
    {
        SequenceBitmapRenderDataErrorCode code{};
        SequenceNodeId node{};
        data::DataId contentsDataId{};
        std::string detail;
    };
    struct SequenceBitmapRenderItem
    {
        SequenceNodeId node{};
        data::DataId contentsDataId{};
        std::uint16_t priority{};
        std::int32_t clock{};
        Matrix2D worldTransform{};
        data::LegacyBitmapMetadata metadata{};
        data::SharedDataBytes bytes;
    };

    [[nodiscard]] std::expected<std::vector<SequenceBitmapRenderItem>,
        SequenceBitmapRenderDataError> collectSequenceBitmapRenderData(
            const SequenceRuntime& runtime,
            std::shared_ptr<const data::ResourceSnapshot> resources);
}
