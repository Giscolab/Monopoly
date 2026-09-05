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
#include <string>
#include <unordered_map>
#include <vector>

namespace monopoly::data
{
    enum class MeshRuntimeErrorCode
    {
        MissingSource,
        MissingResources,
        SourceLoadFailed,
        TriangleDecodeFailed,
        TextureDecodeFailed,
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
    };

    struct MeshMaterial
    {
        std::uint32_t rawDiffuse{};
        std::array<float, 4> diffuse{1.0F, 1.0F, 1.0F, 1.0F};
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

    private:
        std::shared_ptr<const LegacyMeshData> source_;
        std::vector<MeshVertex> vertices_;
        std::vector<MeshGroupRuntime> groups_;
        MeshBounds bounds_{};
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

    struct MeshRuntimeAsset
    {
        DataId dataId{};
        std::shared_ptr<const MeshXRuntime> mesh;
        std::shared_ptr<const MeshRenderData> renderData;
    };

    // Cache scoped to one immutable ResourceSnapshot. A published replacement
    // therefore cannot silently change the bytes behind an existing asset.
    class MeshRuntimeCache final
    {
    public:
        explicit MeshRuntimeCache(std::shared_ptr<const ResourceSnapshot> resources,
            MeshTextureResolver textureResolver = {}, MeshRuntimeLimits limits = {});
        [[nodiscard]] std::expected<std::shared_ptr<const MeshRuntimeAsset>, MeshRuntimeError>
        resolve(DataId id);
        [[nodiscard]] std::size_t size() const noexcept;
        void clear() noexcept;
        [[nodiscard]] std::shared_ptr<const ResourceSnapshot> resources() const noexcept;
    private:
        std::shared_ptr<const ResourceSnapshot> resources_;
        MeshTextureResolver textureResolver_;
        MeshRuntimeLimits limits_;
        std::unordered_map<DataId, std::shared_ptr<const MeshRuntimeAsset>> assets_;
    };
}
