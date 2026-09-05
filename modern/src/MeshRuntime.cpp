#include "MeshRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace monopoly::data
{
    namespace
    {
        constexpr float FixedNormalScale = 4096.0F;

        MeshRuntimeError runtimeError(MeshRuntimeErrorCode code, std::string detail)
        {
            MeshRuntimeError result;
            result.code = code;
            result.detail = std::move(detail);
            return result;
        }

        MeshMaterial materialFrom(std::uint32_t raw)
        {
            MeshMaterial result;
            result.rawDiffuse = raw;
            result.diffuse = {
                static_cast<float>(raw & 0xFFU) / 255.0F,
                static_cast<float>((raw >> 8U) & 0xFFU) / 255.0F,
                static_cast<float>((raw >> 16U) & 0xFFU) / 255.0F,
                1.0F
            };
            return result;
        }

        MeshVertex vertexFrom(const HmdTriangle& triangle, std::size_t index,
            const std::optional<MeshTextureRegion>& texture)
        {
            const auto& sourceVertex = triangle.vertices[index];
            const auto& sourceNormal = triangle.normals[index];
            MeshVertex result;
            result.position = {
                static_cast<float>(sourceVertex.x),
                -static_cast<float>(sourceVertex.y),
                static_cast<float>(sourceVertex.z)
            };
            result.normal = {
                static_cast<float>(sourceNormal.x) / FixedNormalScale,
                -static_cast<float>(sourceNormal.y) / FixedNormalScale,
                static_cast<float>(sourceNormal.z) / FixedNormalScale
            };
            if (texture)
            {
                result.uv = {
                    (static_cast<float>(triangle.texturePoints[index].u) -
                        static_cast<float>(texture->x)) /
                        static_cast<float>(texture->width),
                    (static_cast<float>(triangle.texturePoints[index].v) -
                        static_cast<float>(texture->y)) /
                        static_cast<float>(texture->height)
                };
            }
            return result;
        }

        bool sameVertex(const MeshVertex& left, const MeshVertex& right,
            bool textured) noexcept
        {
            if (left.position != right.position || left.normal != right.normal)
                return false;
            // NewMesh.cpp::FindVertex deliberately ignores UV for untextured
            // vertices, but requires an exact match when texture coordinates exist.
            return !textured || left.uv == right.uv;
        }

        bool sameMaterial(const MeshMaterial& left, const MeshMaterial& right) noexcept
        {
            return left.rawDiffuse == right.rawDiffuse;
        }
    }

    std::expected<MeshXRuntime, MeshRuntimeError> MeshXRuntime::build(
        std::shared_ptr<const LegacyMeshData> source,
        MeshTextureResolver textureResolver, MeshRuntimeLimits limits)
    {
        if (!source)
            return std::unexpected(runtimeError(MeshRuntimeErrorCode::MissingSource,
                "MESHX runtime requires immutable HMD source ownership"));
        if (limits.maximumVertices == 0)
            return std::unexpected(runtimeError(MeshRuntimeErrorCode::VertexLimitExceeded,
                "MESHX vertex budget must be nonzero"));
        if (limits.maximumGroups == 0)
            return std::unexpected(runtimeError(MeshRuntimeErrorCode::GroupLimitExceeded,
                "MESHX group budget must be nonzero"));
        if (limits.maximumIndices < 3)
            return std::unexpected(runtimeError(MeshRuntimeErrorCode::IndexLimitExceeded,
                "MESHX index budget must allow at least one triangle"));

        MeshXRuntime result;
        result.source_ = std::move(source);
        std::size_t skippedUnsupported{};
        std::size_t skippedTextured{};
        std::size_t totalIndices{};

        for (std::size_t primitiveIndex = 0;
            primitiveIndex < result.source_->primitives().size(); ++primitiveIndex)
        {
            const auto& primitive = result.source_->primitives()[primitiveIndex];
            for (std::size_t sectionIndex = 0;
                sectionIndex < primitive.sections.size(); ++sectionIndex)
            {
                const auto& section = primitive.sections[sectionIndex];
                for (std::size_t triangleIndex = 0;
                    triangleIndex < section.elementCount; ++triangleIndex)
                {
                    const auto triangle = result.source_->triangle(
                        primitiveIndex, sectionIndex, triangleIndex);
                    if (!triangle)
                    {
                        if (triangle.error().code == MeshDataErrorCode::UnsupportedSection)
                        {
                            ++skippedUnsupported;
                            break;
                        }
                        auto failure = runtimeError(MeshRuntimeErrorCode::TriangleDecodeFailed,
                            "HMD triangle could not be converted into MESHX runtime geometry");
                        failure.sourceError = triangle.error();
                        failure.skippedUnsupportedSections = skippedUnsupported;
                        failure.skippedTexturedTriangles = skippedTextured;
                        return std::unexpected(std::move(failure));
                    }

                    std::optional<MeshTextureRegion> texture;
                    const bool textured = triangle->texturePage != 0xFFFFU;
                    if (textured)
                    {
                        if (textureResolver)
                            texture = textureResolver({triangle->texturePage,
                                triangle->texturePoints[0].u,
                                triangle->texturePoints[0].v});
                        if (!texture)
                        {
                            // NewMesh.cpp::AddTriangle returns without adding a face
                            // when FindTexture cannot resolve the historical TIM region.
                            ++skippedTextured;
                            continue;
                        }
                        if (texture->width == 0 || texture->height == 0 ||
                            texture->page != triangle->texturePage)
                        {
                            auto failure = runtimeError(
                                MeshRuntimeErrorCode::InvalidTextureRegion,
                                "resolved HMD texture region is empty or belongs to another page");
                            failure.skippedUnsupportedSections = skippedUnsupported;
                            failure.skippedTexturedTriangles = skippedTextured;
                            return std::unexpected(std::move(failure));
                        }
                    }

                    const auto material = materialFrom(triangle->colours[0]);
                    auto group = std::find_if(result.groups_.begin(), result.groups_.end(),
                        [&](const MeshGroupRuntime& candidate)
                        {
                            return candidate.texture == texture &&
                                sameMaterial(candidate.material, material);
                        });
                    if (group == result.groups_.end())
                    {
                        if (result.groups_.size() >= limits.maximumGroups)
                            return std::unexpected(runtimeError(
                                MeshRuntimeErrorCode::GroupLimitExceeded,
                                "MESHX material/texture group budget exceeded"));
                        result.groups_.push_back({material, texture, {}});
                        group = std::prev(result.groups_.end());
                    }
                    if (totalIndices > limits.maximumIndices - 3)
                        return std::unexpected(runtimeError(
                            MeshRuntimeErrorCode::IndexLimitExceeded,
                            "MESHX total index budget exceeded"));

                    for (std::size_t vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
                    {
                        const auto vertex = vertexFrom(*triangle, vertexIndex, texture);
                        const auto found = std::find_if(result.vertices_.begin(),
                            result.vertices_.end(), [&](const MeshVertex& candidate)
                            { return sameVertex(candidate, vertex, textured); });
                        std::size_t index{};
                        if (found == result.vertices_.end())
                        {
                            if (result.vertices_.size() >= limits.maximumVertices ||
                                result.vertices_.size() >=
                                    static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
                                return std::unexpected(runtimeError(
                                    MeshRuntimeErrorCode::VertexLimitExceeded,
                                    "MESHX vertex budget or 32-bit index range exceeded"));
                            index = result.vertices_.size();
                            result.vertices_.push_back(vertex);
                        }
                        else index = static_cast<std::size_t>(
                            std::distance(result.vertices_.begin(), found));
                        group->indices.push_back(static_cast<std::uint32_t>(index));
                    }
                    totalIndices += 3;
                }
            }
        }

        if (result.vertices_.empty())
        {
            auto failure = runtimeError(MeshRuntimeErrorCode::NoRenderableGeometry,
                "HMD did not produce any supported renderable MESHX geometry");
            failure.skippedUnsupportedSections = skippedUnsupported;
            failure.skippedTexturedTriangles = skippedTextured;
            return std::unexpected(std::move(failure));
        }

        result.bounds_.minimum = result.vertices_.front().position;
        result.bounds_.maximum = result.vertices_.front().position;
        for (const auto& vertex : result.vertices_)
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                result.bounds_.minimum[axis] = std::min(
                    result.bounds_.minimum[axis], vertex.position[axis]);
                result.bounds_.maximum[axis] = std::max(
                    result.bounds_.maximum[axis], vertex.position[axis]);
            }
        return result;
    }

    std::shared_ptr<const LegacyMeshData> MeshXRuntime::source() const noexcept
    { return source_; }
    const std::vector<MeshVertex>& MeshXRuntime::vertices() const noexcept
    { return vertices_; }
    const std::vector<MeshGroupRuntime>& MeshXRuntime::groups() const noexcept
    { return groups_; }
    const MeshBounds& MeshXRuntime::bounds() const noexcept
    { return bounds_; }

    MeshRenderData makeMeshRenderData(const MeshXRuntime& mesh)
    {
        MeshRenderData result;
        result.vertices = mesh.vertices();
        result.bounds = mesh.bounds();
        for (const auto& group : mesh.groups())
        {
            const auto first = result.indices.size();
            result.indices.insert(result.indices.end(), group.indices.begin(), group.indices.end());
            result.batches.push_back({first, group.indices.size(),
                group.material, group.texture});
        }
        return result;
    }

    MeshRuntimeCache::MeshRuntimeCache(
        std::shared_ptr<const ResourceSnapshot> resources,
        MeshTextureResolver textureResolver, MeshRuntimeLimits limits)
        : resources_(std::move(resources)), textureResolver_(std::move(textureResolver)),
          limits_(limits)
    {
    }

    std::expected<std::shared_ptr<const MeshRuntimeAsset>, MeshRuntimeError>
    MeshRuntimeCache::resolve(DataId id)
    {
        if (!resources_)
            return std::unexpected(runtimeError(MeshRuntimeErrorCode::MissingResources,
                "MESHX cache requires an immutable resource snapshot"));
        if (const auto found = assets_.find(id); found != assets_.end())
            return found->second;

        auto source = openLegacyMeshData(resources_->banks(), id);
        if (!source)
        {
            auto failure = runtimeError(MeshRuntimeErrorCode::SourceLoadFailed,
                "MESHX cache could not open the requested HMD DATA item");
            failure.sourceError = source.error();
            return std::unexpected(std::move(failure));
        }
        auto immutableSource = std::make_shared<const LegacyMeshData>(std::move(*source));
        auto built = MeshXRuntime::build(immutableSource, textureResolver_, limits_);
        if (!built) return std::unexpected(built.error());
        auto mesh = std::make_shared<const MeshXRuntime>(std::move(*built));
        auto renderData = std::make_shared<const MeshRenderData>(makeMeshRenderData(*mesh));
        auto asset = std::make_shared<const MeshRuntimeAsset>(
            MeshRuntimeAsset{id, std::move(mesh), std::move(renderData)});
        assets_.emplace(id, asset);
        return asset;
    }

    std::size_t MeshRuntimeCache::size() const noexcept { return assets_.size(); }
    void MeshRuntimeCache::clear() noexcept { assets_.clear(); }
    std::shared_ptr<const ResourceSnapshot> MeshRuntimeCache::resources() const noexcept
    { return resources_; }
}
