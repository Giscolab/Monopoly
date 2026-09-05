#pragma once

#include "LegacyDataArchive.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace monopoly::data
{
    enum class MeshDataErrorCode
    {
        None,
        MissingPayload,
        HeaderTruncated,
        AlreadyMapped,
        RangeOutOfBounds,
        InvalidPrimitiveHeader,
        InvalidSectionSize,
        PrimitiveCycle,
        RecordLimitExceeded,
        IndexOutOfRange,
        UnsupportedSection,
        ExpectedOffset,
        DataLoadFailed,
        TypeMismatch
    };

    struct MeshDataError
    {
        MeshDataErrorCode code{ MeshDataErrorCode::None };
        std::size_t offset{};
        std::string detail;
        std::optional<DataError> dataError;
    };

    [[nodiscard]] std::string_view meshDataErrorCodeName(
        MeshDataErrorCode code) noexcept;

    struct MeshParseLimits
    {
        // Allocation/work budgets, not restrictions of the historical format.
        std::size_t maximumPrimitives{ 65'536 };
        std::size_t maximumSections{ 262'144 };
        std::size_t maximumHeaderFields{ 262'144 };
    };

    struct HmdHeaderField
    {
        std::uint32_t rawValue{};
        [[nodiscard]] bool isOffset() const noexcept
        {
            return (rawValue & 0x8000'0000U) != 0;
        }
    };

    struct HmdPrimitiveHeader
    {
        std::size_t offset{};
        std::vector<HmdHeaderField> fields;
    };

    struct HmdDataSection
    {
        std::size_t offset{};
        std::uint32_t type{};
        std::uint32_t sizeWords{};
        std::uint16_t elementCount{};
        bool scanRequired{};
    };

    struct HmdPrimitive
    {
        std::size_t offset{};
        std::optional<std::size_t> nextOffset;
        std::size_t headerIndex{};
        bool processRequired{};
        std::vector<HmdDataSection> sections;
    };

    struct HmdShortVector
    {
        std::int16_t x{};
        std::int16_t y{};
        std::int16_t z{};
        std::int16_t padding{};
    };

    struct HmdTexturePoint
    {
        std::uint8_t u{};
        std::uint8_t v{};
    };

    struct HmdMimeDiffBlock
    {
        std::size_t offset{};
        std::uint32_t startIndex{};
        std::vector<HmdShortVector> diffs;
    };

    // One entry corresponds to legacy pose index 1 + vector index; pose 0
    // is always the undeformed base mesh. Vertex/normal blocks pair by
    // global ordinal, matching hmdload.cpp AddVertex/NormalDiffBlock.
    struct HmdMimePose
    {
        std::optional<HmdMimeDiffBlock> vertex;
        std::optional<HmdMimeDiffBlock> normal;
    };

    struct HmdMimeLimits
    {
        std::size_t maximumDiffBlocks{65'536};
        std::size_t maximumVectors{1'000'000};
    };

    struct HmdTriangle
    {
        std::array<std::uint32_t, 3> colours{};
        std::uint16_t texturePage{ 0xFFFF };
        std::array<HmdTexturePoint, 3> texturePoints{};
        std::array<std::uint16_t, 3> vertexIndices{};
        std::array<std::uint16_t, 3> normalIndices{};
        std::array<HmdShortVector, 3> vertices{};
        std::array<HmdShortVector, 3> normals{};
    };

    struct HmdTextureImage
    {
        std::uint16_t texturePage{};
        std::uint16_t rawX{};
        std::uint16_t rawY{};
        std::int32_t logicalX{};
        std::int32_t logicalY{};
        std::uint32_t width{};
        std::uint32_t height{};
        // RGBA8 conversion of the source 8-bit CLUT image. NewMesh.cpp does
        // not use the PSX transparency bit when constructing its DIB texture.
        std::vector<std::uint8_t> rgba;
    };

    // Immutable, CPU-owned inspection of the unmapped HMD disk payload.
    // All physical pointers are little-endian u32 DWORD offsets, never native
    // pointers. MESHX is the runtime postload object, not another disk format
    // (Source/artlib/L_Data.cpp:7965-8026).
    class LegacyMeshData final
    {
    public:
        [[nodiscard]] static std::expected<LegacyMeshData, MeshDataError>
        parse(SharedDataBytes bytes, MeshParseLimits limits = {});

        [[nodiscard]] std::uint32_t versionId() const noexcept;
        [[nodiscard]] std::span<const std::optional<std::size_t>>
        blockRoots() const noexcept;
        [[nodiscard]] std::span<const HmdPrimitiveHeader>
        headers() const noexcept;
        [[nodiscard]] std::span<const HmdPrimitive> primitives() const noexcept;
        [[nodiscard]] std::span<const std::byte> bytes() const noexcept;

        [[nodiscard]] std::expected<std::span<const std::byte>, MeshDataError>
        sectionBytes(std::size_t primitiveIndex, std::size_t sectionIndex) const;

        // Decodes one category-0 triangle with normals, as consumed by
        // NewMesh.cpp:272-361. Preserves raw coordinates/normals/UV: no GPU,
        // texture resolution, axis conversion or interpolation is implied.
        // Structural parse alone does not validate category-specific payloads.
        // SVECTOR arrays have no declared length here: each referenced record
        // is checked against the owning HMD bytes, not an invented vertex count.
        [[nodiscard]] std::expected<HmdTriangle, MeshDataError> triangle(
            std::size_t primitiveIndex, std::size_t sectionIndex,
            std::size_t triangleIndex) const;

        // Category-2 GsUIMG1 images as consumed by NewMesh.cpp::ProcessHMDImage.
        // Header fields are IMAGE TOP and CLUT TOP word-offset bases; image
        // records remain immutable disk data and are converted without GDI.
        // Decodes only the vertex/normal MIMe types used by Monopoly
        // (GsVtxMIMe/GsNrmMIMe). Primitive chains are consumed tail-first
        // like HMD_MapUnit; reset/joint MIMe remain intentionally opaque.
        [[nodiscard]] std::expected<std::vector<HmdMimePose>, MeshDataError>
            mimePoses(HmdMimeLimits limits = {}) const;

        [[nodiscard]] std::expected<std::vector<HmdTextureImage>, MeshDataError>
            textureImages() const;

    private:
        LegacyMeshData() = default;
        SharedDataBytes owner_;
        std::uint32_t versionId_{};
        std::vector<std::optional<std::size_t>> blockRoots_;
        std::vector<HmdPrimitiveHeader> headers_;
        std::vector<HmdPrimitive> primitives_;
    };

    // Keeps the DAT lease after unmount/clear; never treats a MeshX pointer
    // payload as serialized HMD data.
    [[nodiscard]] std::expected<LegacyMeshData, MeshDataError>
    openLegacyMeshData(const DataBankRegistry& registry, DataId id,
        MeshParseLimits limits = {});
}
