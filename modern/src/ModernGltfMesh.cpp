#include "ModernGltfMesh.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace monopoly::data
{
    namespace
    {
        [[nodiscard]] MeshRuntimeError error(
            MeshRuntimeErrorCode code,
            std::string detail)
        {
            MeshRuntimeError result;
            result.code = code;
            result.detail = std::move(detail);
            return result;
        }


        [[nodiscard]] MeshMaterial materialFor(
            const fastgltf::Asset& asset,
            const fastgltf::Primitive& primitive)
        {
            MeshMaterial result;
            if (!primitive.materialIndex ||
                *primitive.materialIndex >= asset.materials.size())
                return result;

            const auto& factor =
                asset.materials[*primitive.materialIndex]
                    .pbrData.baseColorFactor;

            for (std::size_t index = 0; index < 4; ++index)
                result.diffuse[index] =
                    static_cast<float>(factor[index]);

            const auto channel = [](float value)
            {
                return static_cast<std::uint32_t>(
                    std::lround(
                        std::clamp(value, 0.0F, 1.0F) * 255.0F));
            };
            result.rawDiffuse =
                channel(result.diffuse[0]) |
                (channel(result.diffuse[1]) << 8U) |
                (channel(result.diffuse[2]) << 16U);
            return result;
        }


        [[nodiscard]] fastgltf::math::fvec3 transformPosition(
            const fastgltf::math::fmat4x4& world,
            fastgltf::math::fvec3 position,
            float unitsPerMeter) noexcept
        {
            auto transformed = world * fastgltf::math::fvec4(
                position[0], position[1], position[2], 1.0F);
            return fastgltf::math::fvec3(
                transformed[0] * unitsPerMeter,
                transformed[1] * unitsPerMeter,
                transformed[2] * unitsPerMeter);
        }


        [[nodiscard]] fastgltf::math::fvec3 transformNormal(
            const fastgltf::math::fmat4x4& world,
            fastgltf::math::fvec3 normal) noexcept
        {
            const auto normalMatrix = fastgltf::math::transpose(
                fastgltf::math::inverse(
                    fastgltf::math::fmat3x3(world)));
            return fastgltf::math::normalize(normalMatrix * normal);
        }


        void extendBounds(
            MeshBounds& bounds,
            const MeshVertex& vertex,
            bool& initialized) noexcept
        {
            if (!initialized)
            {
                bounds.minimum = vertex.position;
                bounds.maximum = vertex.position;
                initialized = true;
                return;
            }
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                bounds.minimum[axis] =
                    std::min(bounds.minimum[axis], vertex.position[axis]);
                bounds.maximum[axis] =
                    std::max(bounds.maximum[axis], vertex.position[axis]);
            }
        }


        [[nodiscard]] bool hasUnsupportedMaterialTexture(
            const fastgltf::Asset& asset,
            const fastgltf::Primitive& primitive)
        {
            if (!primitive.materialIndex ||
                *primitive.materialIndex >= asset.materials.size())
                return false;
            const auto& material =
                asset.materials[*primitive.materialIndex];
            return material.pbrData.baseColorTexture.has_value() ||
                material.pbrData.metallicRoughnessTexture.has_value() ||
                material.normalTexture.has_value() ||
                material.emissiveTexture.has_value();
        }
    }


    std::expected<
        std::shared_ptr<const MeshRenderData>,
        MeshRuntimeError>
    loadModernGltfMesh(
        const std::filesystem::path& path,
        ModernGltfLoadOptions options)
    {
        if (path.empty() || options.unitsPerMeter <= 0.0F ||
            !std::isfinite(options.unitsPerMeter))
            return std::unexpected(error(
                MeshRuntimeErrorCode::ModernAssetInvalid,
                "invalid modern GLB path or world scale"));

        fastgltf::GltfFileStream stream(path);
        if (!stream.isOpen())
            return std::unexpected(error(
                MeshRuntimeErrorCode::SourceLoadFailed,
                "modern GLB file cannot be opened: " +
                    path.string()));

        fastgltf::Parser parser;
        auto loaded = parser.loadGltfBinary(
            stream,
            path.parent_path(),
            fastgltf::Options::GenerateMeshIndices);
        if (loaded.error() != fastgltf::Error::None ||
            loaded.get_if() == nullptr)
            return std::unexpected(error(
                MeshRuntimeErrorCode::ModernAssetInvalid,
                std::string("fastgltf: ") +
                    std::string(fastgltf::getErrorMessage(
                        loaded.error()))));

        auto asset = std::move(loaded.get());
        if (asset.scenes.empty() || asset.nodes.empty())
            return std::unexpected(error(
                MeshRuntimeErrorCode::NoRenderableGeometry,
                "modern GLB has no scene nodes"));

        auto result = std::make_shared<MeshRenderData>();
        bool boundsInitialized = false;
        bool failed = false;
        MeshRuntimeError failure{};

        const auto sceneIndex =
            asset.defaultScene.value_or(std::size_t{0});
        if (sceneIndex >= asset.scenes.size())
            return std::unexpected(error(
                MeshRuntimeErrorCode::ModernAssetInvalid,
                "modern GLB default scene is out of range"));

        fastgltf::iterateSceneNodes(
            asset,
            sceneIndex,
            fastgltf::math::fmat4x4(1.0F),
            [&](fastgltf::Node& node,
                const fastgltf::math::fmat4x4& world)
            {
                if (failed || !node.meshIndex)
                    return;
                if (*node.meshIndex >= asset.meshes.size())
                {
                    failed = true;
                    failure = error(
                        MeshRuntimeErrorCode::ModernAssetInvalid,
                        "modern GLB node references an invalid mesh");
                    return;
                }

                const auto& mesh = asset.meshes[*node.meshIndex];
                for (const auto& primitive : mesh.primitives)
                {
                    if (failed) return;
                    if (primitive.type != fastgltf::PrimitiveType::Triangles)
                    {
                        failed = true;
                        failure = error(
                            MeshRuntimeErrorCode::ModernAssetUnsupported,
                            "modern GLB currently supports triangle primitives only");
                        return;
                    }
                    if (!primitive.targets.empty())
                    {
                        failed = true;
                        failure = error(
                            MeshRuntimeErrorCode::ModernAssetUnsupported,
                            "modern GLB morph targets are reserved for the animation pass");
                        return;
                    }
                    if (hasUnsupportedMaterialTexture(asset, primitive))
                    {
                        failed = true;
                        failure = error(
                            MeshRuntimeErrorCode::ModernAssetUnsupported,
                            "textured/PBR GLB material requires the modern material pipeline");
                        return;
                    }

                    const auto position = primitive.findAttribute("POSITION");
                    const auto normal = primitive.findAttribute("NORMAL");
                    if (position == primitive.attributes.end() ||
                        normal == primitive.attributes.end() ||
                        !primitive.indicesAccessor)
                    {
                        failed = true;
                        failure = error(
                            MeshRuntimeErrorCode::ModernAssetInvalid,
                            "modern GLB primitive requires POSITION, NORMAL and indices");
                        return;
                    }

                    const auto& positionAccessor =
                        asset.accessors[position->accessorIndex];
                    const auto& normalAccessor =
                        asset.accessors[normal->accessorIndex];
                    if (positionAccessor.count != normalAccessor.count)
                    {
                        failed = true;
                        failure = error(
                            MeshRuntimeErrorCode::ModernAssetInvalid,
                            "modern GLB position/normal counts differ");
                        return;
                    }

                    if (result->vertices.size() >
                            options.limits.maximumVertices -
                            std::min(
                                options.limits.maximumVertices,
                                positionAccessor.count))
                    {
                        failed = true;
                        failure = error(
                            MeshRuntimeErrorCode::VertexLimitExceeded,
                            "modern GLB exceeds mesh vertex budget");
                        return;
                    }

                    const auto baseVertex = result->vertices.size();
                    result->vertices.resize(
                        baseVertex + positionAccessor.count);

                    const auto uv = primitive.findAttribute("TEXCOORD_0");
                    for (std::size_t index = 0;
                        index < positionAccessor.count;
                        ++index)
                    {
                        const auto sourcePosition =
                            fastgltf::getAccessorElement<
                                fastgltf::math::fvec3>(
                                    asset, positionAccessor, index);
                        const auto sourceNormal =
                            fastgltf::getAccessorElement<
                                fastgltf::math::fvec3>(
                                    asset, normalAccessor, index);

                        const auto worldPosition = transformPosition(
                            world, sourcePosition, options.unitsPerMeter);
                        const auto worldNormal =
                            transformNormal(world, sourceNormal);

                        auto& vertex = result->vertices[baseVertex + index];
                        vertex.position = {
                            worldPosition[0],
                            worldPosition[1],
                            worldPosition[2]};
                        vertex.normal = {
                            worldNormal[0],
                            worldNormal[1],
                            worldNormal[2]};

                        if (uv != primitive.attributes.end())
                        {
                            const auto sourceUv =
                                fastgltf::getAccessorElement<
                                    fastgltf::math::fvec2>(
                                        asset,
                                        asset.accessors[uv->accessorIndex],
                                        index);
                            vertex.uv = {
                                sourceUv[0],
                                sourceUv[1]};
                        }
                        extendBounds(
                            result->bounds,
                            vertex,
                            boundsInitialized);
                    }

                    const auto& indexAccessor =
                        asset.accessors[*primitive.indicesAccessor];
                    if (result->indices.size() >
                            options.limits.maximumIndices -
                            std::min(
                                options.limits.maximumIndices,
                                indexAccessor.count))
                    {
                        failed = true;
                        failure = error(
                            MeshRuntimeErrorCode::IndexLimitExceeded,
                            "modern GLB exceeds mesh index budget");
                        return;
                    }
                    if (result->batches.size() >=
                        options.limits.maximumGroups)
                    {
                        failed = true;
                        failure = error(
                            MeshRuntimeErrorCode::GroupLimitExceeded,
                            "modern GLB exceeds mesh batch budget");
                        return;
                    }

                    const auto firstIndex = result->indices.size();
                    result->indices.reserve(
                        firstIndex + indexAccessor.count);
                    for (std::size_t index = 0;
                        index < indexAccessor.count;
                        ++index)
                    {
                        const auto sourceIndex =
                            fastgltf::getAccessorElement<std::uint32_t>(
                                asset, indexAccessor, index);
                        if (sourceIndex >= positionAccessor.count)
                        {
                            failed = true;
                            failure = error(
                                MeshRuntimeErrorCode::ModernAssetInvalid,
                                "modern GLB index exceeds primitive vertex count");
                            return;
                        }
                        result->indices.push_back(
                            static_cast<std::uint32_t>(
                                baseVertex + sourceIndex));
                    }

                    result->batches.push_back(MeshRenderBatch{
                        firstIndex,
                        indexAccessor.count,
                        materialFor(asset, primitive),
                        std::nullopt
                    });
                }
            });

        if (failed)
            return std::unexpected(std::move(failure));
        if (!boundsInitialized ||
            result->vertices.empty() ||
            result->indices.empty() ||
            result->batches.empty())
            return std::unexpected(error(
                MeshRuntimeErrorCode::NoRenderableGeometry,
                "modern GLB contains no renderable triangle geometry"));

        return std::const_pointer_cast<const MeshRenderData>(result);
    }
}
