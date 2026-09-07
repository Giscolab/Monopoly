#pragma once

#include "LegacyBitmap.hpp"
#include "ResourceRuntime.hpp"
#include "SequenceRuntime.hpp"

#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace monopoly::data
{
    class RuntimeBitmapStore;
    struct BitmapRuntimeAsset;
}

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
    struct SequenceBitmapMetadata
    {
        data::LegacyDataType type{data::LegacyDataType::Unknown};
        std::uint32_t width{};
        std::uint32_t height{};
        std::int32_t originX{};
        std::int32_t originY{};
        std::uint16_t bitsPerPixel{};
    };

    struct SequenceBitmapRenderItem
    {
        SequenceNodeId node{};
        data::DataId contentsDataId{};
        std::uint16_t priority{};
        std::int32_t clock{};
        Matrix2D worldTransform{};
        SequenceBitmapMetadata metadata{};
        data::SharedDataBytes bytes;
        std::shared_ptr<const data::BitmapRuntimeAsset> runtimeAsset;
    };

    [[nodiscard]] std::expected<std::vector<SequenceBitmapRenderItem>,
        SequenceBitmapRenderDataError> collectSequenceBitmapRenderData(
            const SequenceRuntime& runtime,
            std::shared_ptr<const data::ResourceSnapshot> resources,
            const data::RuntimeBitmapStore* runtimeBitmaps = nullptr);
}
