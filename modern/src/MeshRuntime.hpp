#pragma once

#include "LegacyMeshData.hpp"
#include "ResourceRuntime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace monopoly::data
{
    enum class MeshRuntimeErrorCode
    {
        MissingSource,
        MissingResources,
        SourceLoadFailed,
        ModernAssetInvalid,
        ModernAssetUnsupported,
        TriangleDecodeFailed,
        TextureDecodeFailed,
        MimeDecodeFailed,
        InvalidPose,
        InvalidTextureRegion,
        VertexLimitExceeded,
        GroupLimitExceeded,
        IndexLimitExceeded,
        NoRenderableGeometry
    };

    struct MeshRuntimeError
    {
        MeshRuntimeErrorCode code{};
        std::string detail;
        std::optional<MeshDataError> sourceError;
        std::size_t skippedUnsupportedSections{};
        std::size_t skippedTexturedTriangles{};
    };

    struct MeshRuntimeLimits
    {
        std::size_t maximumVertices{1'000'000};
        std::size_t maximumGroups{65'536};
        std::size_t maximumIndices{3'000'000};
    };

    struct MeshTextureLookup
    {
        std::uint16_t page{};
        std::uint8_t u{};
        std::uint8_t v{};
    };

    struct MeshTextureRegion
    {
        std::uint64_t key{};
        std::uint16_t page{};
        std::int32_t x{};
        std::int32_t y{};
        std::uint32_t width{};
        std::uint32_t height{};
        // Present for HMD-embedded GsUIMG1 textures. External resolvers may
        // leave this empty and keep their existing renderer-owned identity.
        std::shared_ptr<const HmdTextureImage> sourceImage;

        [[nodiscard]] bool operator==(const MeshTextureRegion& other) const noexcept
        {
            return key == other.key && page == other.page && x == other.x &&
                y == other.y && width == other.width && height == other.height;
        }
    };

    using MeshTextureResolver = std::function<std::optional<MeshTextureRegion>(
        const MeshTextureLookup&)>;

    struct MeshVertex
    {
        std::array<float, 3> position{};
        std::array<float, 3> normal{};
        std::array<float, 2> uv{-1.0F, -1.0F};
        // w == 0 means no authored tangent; the modern shader derives a basis.
        std::array<float, 4> tangent{};
    };

    enum class MeshMaterialModel : std::uint8_t
    {
        LegacyDiffuse,
        MetallicRoughness
    };

    struct ModernTextureImage
    {
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<std::uint8_t> rgba;
    };

    enum class ModernTextureColorSpace : std::uint8_t { Linear, Srgb };
    enum class ModernAlphaMode : std::uint8_t { Opaque, Mask };
    enum class ModernTextureWrap : std::uint8_t { Repeat, ClampToEdge, MirroredRepeat };
    enum class ModernTextureFilter : std::uint8_t
    {
        Nearest, Linear, NearestMipmapNearest, LinearMipmapNearest,
        NearestMipmapLinear, LinearMipmapLinear
    };
    struct ModernTextureSampler
    {
        ModernTextureWrap wrapS{ModernTextureWrap::Repeat};
        ModernTextureWrap wrapT{ModernTextureWrap::Repeat};
        ModernTextureFilter minFilter{ModernTextureFilter::Linear};
        ModernTextureFilter magFilter{ModernTextureFilter::Linear};
        [[nodiscard]] bool operator==(const ModernTextureSampler&) const noexcept = default;
    };
    struct ModernTextureBinding
    {
        std::shared_ptr<const ModernTextureImage> image;
        ModernTextureSampler sampler;
        ModernTextureColorSpace colorSpace{ModernTextureColorSpace::Linear};
        std::uint32_t texCoord{};
        // Normal scale or occlusion strength; other maps retain one.
        float scale{1.0F};
    };

    struct MeshMaterial
    {
        MeshMaterialModel model{MeshMaterialModel::LegacyDiffuse};
        std::uint32_t rawDiffuse{};
        std::array<float, 4> diffuse{1.0F, 1.0F, 1.0F, 1.0F};
        float metallic{};
        float roughness{1.0F};
        std::array<float, 3> emissive{};
        float emissiveStrength{1.0F};
        bool doubleSided{};
        ModernAlphaMode alphaMode{ModernAlphaMode::Opaque};
        float alphaCutoff{0.5F};
        std::optional<ModernTextureBinding> baseColorTexture;
        std::optional<ModernTextureBinding> metallicRoughnessTexture;
        std::optional<ModernTextureBinding> normalTexture;
        std::optional<ModernTextureBinding> emissiveTexture;
        std::optional<ModernTextureBinding> occlusionTexture;
    };

    struct MeshGroupRuntime
    {
        MeshMaterial material;
        std::optional<MeshTextureRegion> texture;
        std::vector<std::uint32_t> indices;
    };

    struct MeshBounds
    {
        std::array<float, 3> minimum{};
        std::array<float, 3> maximum{};
    };

    struct MeshPoseData
    {
        std::vector<MeshVertex> vertices;
        MeshBounds bounds{};
    };

    class MeshXRuntime final
    {
    public:
        [[nodiscard]] static std::expected<MeshXRuntime, MeshRuntimeError> build(
            std::shared_ptr<const LegacyMeshData> source,
            MeshTextureResolver textureResolver = {},
            MeshRuntimeLimits limits = {});

        [[nodiscard]] std::shared_ptr<const LegacyMeshData> source() const noexcept;
        [[nodiscard]] const std::vector<MeshVertex>& vertices() const noexcept;
        [[nodiscard]] const std::vector<MeshGroupRuntime>& groups() const noexcept;
        [[nodiscard]] const MeshBounds& bounds() const noexcept;
        [[nodiscard]] std::size_t poseCount() const noexcept;
        [[nodiscard]] std::expected<MeshPoseData, MeshRuntimeError> evaluatePose(
            std::int32_t poseA, std::int32_t poseB, float proportion) const;

        // UDUTILS substitutions replace images after MESHX normalized its UVs.
        // A 256px BMP replacing a 128px HMD image must keep those vertices.
        [[nodiscard]] std::expected<MeshXRuntime, MeshRuntimeError> withTextureImages(
            std::span<const std::shared_ptr<const HmdTextureImage>> images) const;

    private:
        std::shared_ptr<const LegacyMeshData> source_;
        std::vector<MeshVertex> vertices_;
        std::vector<MeshGroupRuntime> groups_;
        MeshBounds bounds_{};
        std::vector<std::array<std::uint16_t, 2>> sourceIndices_;
        std::vector<HmdMimePose> mimePoses_;
    };

    struct MeshRenderBatch
    {
        std::size_t firstIndex{};
        std::size_t indexCount{};
        MeshMaterial material;
        std::optional<MeshTextureRegion> texture;
    };

    struct MeshRenderData
    {
        std::vector<MeshVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<MeshRenderBatch> batches;
        MeshBounds bounds{};
    };

    [[nodiscard]] MeshRenderData makeMeshRenderData(const MeshXRuntime& mesh);
    [[nodiscard]] std::expected<MeshRenderData, MeshRuntimeError> makeMeshRenderData(
        const MeshXRuntime& mesh, std::int32_t poseA, std::int32_t poseB,
        float proportion);

    enum class MeshAssetOrigin : std::uint8_t
    {
        LegacyHmd,
        ModernGltf
    };

    struct MeshRuntimeAsset
    {
        DataId dataId{};
        MeshAssetOrigin origin{MeshAssetOrigin::LegacyHmd};
        // Present only for legacy HMD assets. Modern glTF animation will have
        // its own pose/morph owner rather than pretending to be MIMe data.
        std::shared_ptr<const MeshXRuntime> mesh;
        std::shared_ptr<const MeshRenderData> renderData;
    };

    // Resolver priority is the owning root sequence priority; child render
    // priorities remain independent and preserve historical draw ordering.
    using ModernMeshResolver = std::function<
        std::expected<std::optional<std::shared_ptr<const MeshRenderData>>,
            MeshRuntimeError>(
                DataId,
                std::optional<DataId> rootSequenceDataId,
                std::uint16_t priority)>;

    // Cache scoped to one immutable ResourceSnapshot. A published replacement
    // therefore cannot silently change the bytes behind an existing asset.
    class MeshRuntimeCache final
    {
    public:
        explicit MeshRuntimeCache(std::shared_ptr<const ResourceSnapshot> resources,
            MeshTextureResolver textureResolver = {}, MeshRuntimeLimits limits = {},
            ModernMeshResolver modernMeshResolver = {});
        [[nodiscard]] std::expected<std::shared_ptr<const MeshRuntimeAsset>, MeshRuntimeError>
        resolve(
            DataId id,
            std::optional<DataId> rootSequenceDataId = std::nullopt,
            std::uint16_t priority = 0);
        // GPU rejection is scoped to immutable geometry and complete sequence
        // roots; both survive eviction so later poses do not retry bad assets.
        [[nodiscard]] bool rejectModernAsset(const MeshRenderData* renderData);
        void rejectModernSequence(DataId rootSequenceDataId);
        // Prepare a complete replacement before publishing it; only this DataId
        // changes. Empty images restore the original embedded texture payloads.
        [[nodiscard]] std::expected<void, MeshRuntimeError> replaceTextureImages(
            DataId id, std::span<const std::shared_ptr<const HmdTextureImage>> images);
        [[nodiscard]] std::size_t size() const noexcept;
        // Release only cache-owned decoded meshes and their DAT leases.
        [[nodiscard]] std::size_t releaseUnused() noexcept;
        // Both eviction operations retain configured texture substitutions;
        // resolve reapplies them when a mesh is rebuilt. An empty replacement
        // explicitly restores the embedded textures for the selected DataId.
        void clear() noexcept;
        [[nodiscard]] std::shared_ptr<const ResourceSnapshot> resources() const noexcept;
    private:
        std::shared_ptr<const ResourceSnapshot> resources_;
        MeshTextureResolver textureResolver_;
        MeshRuntimeLimits limits_;
        ModernMeshResolver modernMeshResolver_;
        // Modern and legacy assets must not share one cache entry: the same
        // retail HMD can appear in both an idle sequence (modern override) and
        // a movement sequence (retail fallback).
        std::unordered_map<DataId, std::vector<std::shared_ptr<const MeshRuntimeAsset>>>
            modernAssets_;
        std::vector<std::shared_ptr<const MeshRenderData>> rejectedModernAssets_;
        std::unordered_set<DataId> rejectedModernSequences_;
        std::unordered_map<DataId, std::shared_ptr<const MeshRuntimeAsset>> assets_;
        std::unordered_map<DataId,
            std::vector<std::shared_ptr<const HmdTextureImage>>> textureOverrides_;
    };
}
