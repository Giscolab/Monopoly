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
        std::optional<std::vector<std::shared_ptr<const HmdTextureImage>>> embeddedTextures;

        auto resolveEmbeddedTexture = [&](const MeshTextureLookup& lookup)
            -> std::expected<std::optional<MeshTextureRegion>, MeshRuntimeError>
        {
            if (!embeddedTextures)
            {
                auto decoded = result.source_->textureImages();
                if (!decoded)
                {
                    auto failure = runtimeError(MeshRuntimeErrorCode::TextureDecodeFailed,
                        "embedded HMD texture image could not be decoded");
                    failure.sourceError = decoded.error();
                    failure.skippedUnsupportedSections = skippedUnsupported;
                    failure.skippedTexturedTriangles = skippedTextured;
                    return std::unexpected(std::move(failure));
                }
                embeddedTextures.emplace();
                embeddedTextures->reserve(decoded->size());
                for (auto& image : *decoded)
                    embeddedTextures->push_back(
                        std::make_shared<const HmdTextureImage>(std::move(image)));
            }

            // NewMesh.cpp::FindTexture scans newest-to-oldest and includes edges.
            for (std::size_t index = embeddedTextures->size(); index-- > 0;)
            {
                const auto& image = (*embeddedTextures)[index];
                const auto u = static_cast<std::int32_t>(lookup.u);
                const auto v = static_cast<std::int32_t>(lookup.v);
                const auto right = image->logicalX + static_cast<std::int32_t>(image->width);
                const auto bottom = image->logicalY + static_cast<std::int32_t>(image->height);
                if (lookup.page == image->texturePage &&
                    u >= image->logicalX && u <= right &&
                    v >= image->logicalY && v <= bottom)
                    return std::optional<MeshTextureRegion>{MeshTextureRegion{
                        static_cast<std::uint64_t>(index + 1U), image->texturePage,
                        image->logicalX, image->logicalY, image->width, image->height, image}};
            }
            return std::optional<MeshTextureRegion>{};
        };
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
                        const MeshTextureLookup lookup{triangle->texturePage,
                            triangle->texturePoints[0].u, triangle->texturePoints[0].v};
                        if (textureResolver)
                            texture = textureResolver(lookup);
                        else
                        {
                            auto embedded = resolveEmbeddedTexture(lookup);
                            if (!embedded)
                                return std::unexpected(std::move(embedded.error()));
                            texture = std::move(*embedded);
                        }
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
                            result.sourceIndices_.push_back({
                                triangle->vertexIndices[vertexIndex],
                                triangle->normalIndices[vertexIndex]});
                        }
                        else index = static_cast<std::size_t>(
                            std::distance(result.vertices_.begin(), found));
                        group->indices.push_back(static_cast<std::uint32_t>(index));
                    }
                    totalIndices += 3;
                }
            }
        }

        auto mime = result.source_->mimePoses();
        if (!mime)
        {
            auto failure = runtimeError(MeshRuntimeErrorCode::MimeDecodeFailed,
                "HMD MIMe diff blocks could not be decoded");
            failure.sourceError = mime.error();
            failure.skippedUnsupportedSections = skippedUnsupported;
            failure.skippedTexturedTriangles = skippedTextured;
            return std::unexpected(std::move(failure));
        }
        result.mimePoses_ = std::move(*mime);

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

    std::size_t MeshXRuntime::poseCount() const noexcept
    { return mimePoses_.size() + 1U; }

    std::expected<MeshPoseData, MeshRuntimeError> MeshXRuntime::evaluatePose(
        std::int32_t poseA, std::int32_t poseB, float proportion) const
    {
        const auto validPose = [&](std::int32_t pose)
        {
            return pose >= 0 &&
                static_cast<std::size_t>(pose) <= mimePoses_.size();
        };
        if (!validPose(poseA) || !validPose(poseB))
            return std::unexpected(runtimeError(MeshRuntimeErrorCode::InvalidPose,
                "MESHX pose index is outside base-plus-MIMe pose range"));
        if (vertices_.size() != sourceIndices_.size())
            return std::unexpected(runtimeError(MeshRuntimeErrorCode::InvalidPose,
                "MESHX source-index map no longer matches base vertices"));

        auto applyPose = [&](std::size_t vertexIndex, std::int32_t pose)
        {
            auto value = vertices_[vertexIndex];
            if (pose == 0)
                return value;
            const auto& mime = mimePoses_[static_cast<std::size_t>(pose - 1)];
            const auto sourceVertex = sourceIndices_[vertexIndex][0];
            const auto sourceNormal = sourceIndices_[vertexIndex][1];
            if (mime.vertex && sourceVertex >= mime.vertex->startIndex)
            {
                const auto diffIndex = static_cast<std::size_t>(
                    sourceVertex - mime.vertex->startIndex);
                if (diffIndex < mime.vertex->diffs.size())
                {
                    const auto& diff = mime.vertex->diffs[diffIndex];
                    value.position[0] += static_cast<float>(diff.x);
                    value.position[1] -= static_cast<float>(diff.y);
                    value.position[2] += static_cast<float>(diff.z);
                }
            }
            if (mime.normal && sourceNormal >= mime.normal->startIndex)
            {
                const auto diffIndex = static_cast<std::size_t>(
                    sourceNormal - mime.normal->startIndex);
                if (diffIndex < mime.normal->diffs.size())
                {
                    // hmdload.cpp::SetVertexAndSiblings adds normal MIMe
                    // SVECTOR values directly after the base /4096 conversion.
                    const auto& diff = mime.normal->diffs[diffIndex];
                    value.normal[0] += static_cast<float>(diff.x);
                    value.normal[1] -= static_cast<float>(diff.y);
                    value.normal[2] += static_cast<float>(diff.z);
                }
            }
            return value;
        };

        MeshPoseData result;
        result.vertices.reserve(vertices_.size());
        MeshBounds boundsA{};
        MeshBounds boundsB{};
        bool first = true;
        for (std::size_t index = 0; index < vertices_.size(); ++index)
        {
            const auto a = applyPose(index, poseA);
            const auto b = applyPose(index, poseB);
            auto value = a;
            if (poseA != poseB && proportion != 0.0F)
            {
                if (proportion == 1.0F)
                    value = b;
                else
                {
                    for (std::size_t axis = 0; axis < 3; ++axis)
                    {
                        value.position[axis] = a.position[axis] +
                            (b.position[axis] - a.position[axis]) * proportion;
                        value.normal[axis] = a.normal[axis] +
                            (b.normal[axis] - a.normal[axis]) * proportion;
                    }
                    // Legacy HMD_interpolate copies UVs from pose A.
                    value.uv = a.uv;
                }
            }
            result.vertices.push_back(value);

            if (first)
            {
                boundsA.minimum = boundsA.maximum = a.position;
                boundsB.minimum = boundsB.maximum = b.position;
                first = false;
            }
            else
            {
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    boundsA.minimum[axis] = std::min(boundsA.minimum[axis], a.position[axis]);
                    boundsA.maximum[axis] = std::max(boundsA.maximum[axis], a.position[axis]);
                    boundsB.minimum[axis] = std::min(boundsB.minimum[axis], b.position[axis]);
                    boundsB.maximum[axis] = std::max(boundsB.maximum[axis], b.position[axis]);
                }
            }
        }

        if (poseA == poseB || proportion == 0.0F)
            result.bounds = boundsA;
        else if (proportion == 1.0F)
            result.bounds = boundsB;
        else
        {
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                result.bounds.minimum[axis] = boundsA.minimum[axis] +
                    (boundsB.minimum[axis] - boundsA.minimum[axis]) * proportion;
                result.bounds.maximum[axis] = boundsA.maximum[axis] +
                    (boundsB.maximum[axis] - boundsA.maximum[axis]) * proportion;
            }
        }
        return result;
    }

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

    std::expected<MeshRenderData, MeshRuntimeError> makeMeshRenderData(
        const MeshXRuntime& mesh, std::int32_t poseA, std::int32_t poseB,
        float proportion)
    {
        auto evaluated = mesh.evaluatePose(poseA, poseB, proportion);
        if (!evaluated)
            return std::unexpected(evaluated.error());
        MeshRenderData result;
        result.vertices = std::move(evaluated->vertices);
        result.bounds = evaluated->bounds;
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
