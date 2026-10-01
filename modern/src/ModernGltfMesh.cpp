#include "ModernGltfMesh.hpp"
#include "ModernImageDecoder.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <variant>
#include <unordered_map>
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


        [[nodiscard]] MeshMaterial materialFactors(
            const fastgltf::Asset& asset,
            const fastgltf::Primitive& primitive)
        {
            MeshMaterial result;
            result.model = MeshMaterialModel::MetallicRoughness;
            if (!primitive.materialIndex ||
                *primitive.materialIndex >= asset.materials.size())
                return result;

            const auto& material =
                asset.materials[*primitive.materialIndex];
            const auto& factor = material.pbrData.baseColorFactor;

            for (std::size_t index = 0; index < 4; ++index)
                result.diffuse[index] =
                    static_cast<float>(factor[index]);
            result.metallic = static_cast<float>(
                material.pbrData.metallicFactor);
            result.roughness = static_cast<float>(
                material.pbrData.roughnessFactor);
            for (std::size_t index = 0; index < 3; ++index)
                result.emissive[index] =
                    static_cast<float>(material.emissiveFactor[index]);
            result.emissiveStrength =
                static_cast<float>(material.emissiveStrength);
            result.doubleSided = material.doubleSided;

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


        struct YawRotation
        {
            float sine;
            float cosine;
        };

        [[nodiscard]] YawRotation yawRotation(float yawDegrees) noexcept
        {
            constexpr float DegreesToRadians =
                3.14159265358979323846F / 180.0F;
            const float angle = yawDegrees * DegreesToRadians;
            return {std::sin(angle), std::cos(angle)};
        }

        [[nodiscard]] fastgltf::math::fvec3 rotateAroundY(
            fastgltf::math::fvec3 value,
            const YawRotation& rotation) noexcept
        {
            return fastgltf::math::fvec3(
                rotation.cosine * value[0] + rotation.sine * value[2],
                value[1],
                -rotation.sine * value[0] + rotation.cosine * value[2]);
        }


        [[nodiscard]] fastgltf::math::fvec3 transformPosition(
            const fastgltf::math::fmat4x4& world,
            fastgltf::math::fvec3 position,
            float unitsPerMeter,
            const YawRotation& rotation) noexcept
        {
            const auto transformed = world * fastgltf::math::fvec4(
                position[0], position[1], position[2], 1.0F);
            auto rotated = rotateAroundY(
                fastgltf::math::fvec3(
                    transformed[0],
                    transformed[1],
                    transformed[2]),
                rotation);
            return rotated * unitsPerMeter;
        }


        [[nodiscard]] fastgltf::math::fvec3 transformNormal(
            const fastgltf::math::fmat3x3& normalMatrix,
            fastgltf::math::fvec3 normal,
            const YawRotation& rotation) noexcept
        {
            return fastgltf::math::normalize(
                rotateAroundY(normalMatrix * normal, rotation));
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


        [[nodiscard]] bool validBufferView(const fastgltf::Asset& asset,
            std::size_t index)
        {
            if (index >= asset.bufferViews.size()) return false;
            const auto& view = asset.bufferViews[index];
            if (view.bufferIndex >= asset.buffers.size()) return false;
            const auto bytes = std::visit(fastgltf::visitor{
                [](const auto&) -> std::size_t { return 0; },
                [](const fastgltf::sources::Array& value) { return value.bytes.size_bytes(); },
                [](const fastgltf::sources::Vector& value) { return value.bytes.size(); },
                [](const fastgltf::sources::ByteView& value) { return value.bytes.size(); }
            }, asset.buffers[view.bufferIndex].data);
            return view.byteOffset <= bytes && view.byteLength <= bytes - view.byteOffset;
        }

        [[nodiscard]] bool validAccessor(const fastgltf::Asset& asset,
            std::size_t index, fastgltf::AccessorType type)
        {
            if (index >= asset.accessors.size()) return false;
            const auto& accessor = asset.accessors[index];
            if (accessor.type != type || accessor.count == 0 ||
                accessor.componentType == fastgltf::ComponentType::Invalid)
                return false;
            const auto elementBytes = fastgltf::getElementByteSize(
                accessor.type, accessor.componentType);
            const auto fits = [&](std::size_t viewIndex, std::size_t offset,
                                  std::size_t count, std::size_t size,
                                  bool strided)
            {
                if (!validBufferView(asset, viewIndex)) return false;
                const auto& view = asset.bufferViews[viewIndex];
                const auto stride = strided ? view.byteStride.value_or(size) : size;
                if (size == 0 || stride < size || offset > view.byteLength)
                    return false;
                const auto available = view.byteLength - offset;
                return count == 0 || (size <= available &&
                    count - 1 <= (available - size) / stride);
            };
            if (accessor.bufferViewIndex &&
                !fits(*accessor.bufferViewIndex, accessor.byteOffset,
                    accessor.count, elementBytes, true)) return false;
            if (!accessor.bufferViewIndex && !accessor.sparse) return false;
            if (accessor.sparse)
            {
                const auto& sparse = *accessor.sparse;
                if (sparse.count > accessor.count ||
                    (sparse.indexComponentType != fastgltf::ComponentType::UnsignedByte &&
                     sparse.indexComponentType != fastgltf::ComponentType::UnsignedShort &&
                     sparse.indexComponentType != fastgltf::ComponentType::UnsignedInt) ||
                    !fits(sparse.indicesBufferView, sparse.indicesByteOffset,
                        sparse.count, fastgltf::getComponentByteSize(sparse.indexComponentType), false) ||
                    !fits(sparse.valuesBufferView, sparse.valuesByteOffset,
                        sparse.count, elementBytes, false)) return false;
            }
            return true;
        }

        // Validate the hierarchy before fastgltf's recursive traversal. A deep
        // or cyclic document must not exhaust the stack before geometry checks.
        [[nodiscard]] bool validHierarchy(const fastgltf::Asset& asset)
        {
            struct Entry { std::size_t node; std::size_t nextChild; };
            std::vector<std::uint8_t> colors(asset.nodes.size());
            std::vector<std::size_t> depths(asset.nodes.size(), 1);
            std::vector<Entry> stack;
            for (std::size_t root = 0; root < asset.nodes.size(); ++root)
            {
                if (colors[root] != 0) continue;
                colors[root] = 1;
                stack.push_back({root, 0});
                while (!stack.empty())
                {
                    auto& entry = stack.back();
                    const auto& children = asset.nodes[entry.node].children;
                    if (entry.nextChild == children.size())
                    {
                        for (const auto child : children)
                            depths[entry.node] = std::max(depths[entry.node], depths[child] + 1);
                        if (depths[entry.node] > 1024) return false;
                        colors[entry.node] = 2;
                        stack.pop_back();
                        continue;
                    }
                    const auto child = children[entry.nextChild++];
                    if (child >= asset.nodes.size() || colors[child] == 1)
                        return false;
                    if (colors[child] == 2) continue;
                    if (stack.size() >= 1024) return false;
                    colors[child] = 1;
                    stack.push_back({child, 0});
                }
            }
            for (const auto& scene : asset.scenes)
                for (const auto node : scene.nodeIndices)
                    if (node >= asset.nodes.size()) return false;
            return true;
        }

        using ImageCache = std::unordered_map<std::size_t,
            std::shared_ptr<const ModernTextureImage>>;
        constexpr std::size_t MaximumMeshImageBytes = 256U * 1024U * 1024U;

        [[nodiscard]] std::expected<ModernTextureBinding, MeshRuntimeError> textureBindingFor(
            const fastgltf::Asset& asset, const fastgltf::TextureInfo& info,
            ModernTextureColorSpace colorSpace, float scale,
            ImageCache& images, std::size_t& decodedImageBytes)
        {
            if (info.texCoordIndex != 0 || info.transform)
                return std::unexpected(error(MeshRuntimeErrorCode::ModernAssetUnsupported,
                    "modern GLB maps require TEXCOORD_0 without texture transforms"));
            if (!std::isfinite(scale) || info.textureIndex >= asset.textures.size())
                return std::unexpected(error(MeshRuntimeErrorCode::ModernAssetInvalid,
                    "modern GLB texture reference or map scale is invalid"));
            const auto& texture = asset.textures[info.textureIndex];
            if (!texture.imageIndex || *texture.imageIndex >= asset.images.size())
                return std::unexpected(error(MeshRuntimeErrorCode::ModernAssetInvalid,
                    "modern GLB texture requires a valid image reference"));
            ModernTextureBinding result;
            result.colorSpace = colorSpace;
            result.scale = scale;
            if (texture.samplerIndex)
            {
                if (*texture.samplerIndex >= asset.samplers.size())
                    return std::unexpected(error(MeshRuntimeErrorCode::ModernAssetInvalid,
                        "modern GLB sampler reference is out of range"));
                const auto& sampler = asset.samplers[*texture.samplerIndex];
                const auto wrap = [](fastgltf::Wrap source) -> std::optional<ModernTextureWrap>
                {
                    switch (source)
                    {
                    case fastgltf::Wrap::Repeat: return ModernTextureWrap::Repeat;
                    case fastgltf::Wrap::ClampToEdge: return ModernTextureWrap::ClampToEdge;
                    case fastgltf::Wrap::MirroredRepeat: return ModernTextureWrap::MirroredRepeat;
                    default: return std::nullopt;
                    }
                };
                const auto filter = [](fastgltf::Filter source) -> std::optional<ModernTextureFilter>
                {
                    switch (source)
                    {
                    case fastgltf::Filter::Nearest: return ModernTextureFilter::Nearest;
                    case fastgltf::Filter::Linear: return ModernTextureFilter::Linear;
                    case fastgltf::Filter::NearestMipMapNearest: return ModernTextureFilter::NearestMipmapNearest;
                    case fastgltf::Filter::LinearMipMapNearest: return ModernTextureFilter::LinearMipmapNearest;
                    case fastgltf::Filter::NearestMipMapLinear: return ModernTextureFilter::NearestMipmapLinear;
                    case fastgltf::Filter::LinearMipMapLinear: return ModernTextureFilter::LinearMipmapLinear;
                    default: return std::nullopt;
                    }
                };
                const auto wrapS = wrap(sampler.wrapS), wrapT = wrap(sampler.wrapT);
                const auto min = filter(sampler.minFilter.value_or(fastgltf::Filter::Linear));
                const auto mag = filter(sampler.magFilter.value_or(fastgltf::Filter::Linear));
                if (!wrapS || !wrapT || !min || !mag ||
                    (*mag != ModernTextureFilter::Nearest && *mag != ModernTextureFilter::Linear))
                    return std::unexpected(error(MeshRuntimeErrorCode::ModernAssetInvalid,
                        "modern GLB sampler wrap or filter is invalid"));
                result.sampler = {*wrapS, *wrapT, *min, *mag};
            }
            const auto imageIndex = *texture.imageIndex;
            if (const auto cached = images.find(imageIndex); cached != images.end())
            {
                result.image = cached->second;
                return result;
            }
            const auto* source = std::get_if<fastgltf::sources::BufferView>(
                &asset.images[imageIndex].data);
            if (!source)
                return std::unexpected(error(MeshRuntimeErrorCode::ModernAssetUnsupported,
                    "modern GLB images must be embedded buffer views; external and data URIs are unsupported"));
            if (!validBufferView(asset, source->bufferViewIndex))
                return std::unexpected(error(MeshRuntimeErrorCode::ModernAssetInvalid,
                    "modern GLB image buffer view is outside its embedded payload"));
            ModernImageEncoding encoding;
            if (source->mimeType == fastgltf::MimeType::PNG) encoding = ModernImageEncoding::Png;
            else if (source->mimeType == fastgltf::MimeType::JPEG) encoding = ModernImageEncoding::Jpeg;
            else return std::unexpected(error(MeshRuntimeErrorCode::ModernAssetUnsupported,
                "modern GLB embedded image MIME must be PNG or JPEG"));
            const auto bytes = fastgltf::DefaultBufferDataAdapter{}(asset, source->bufferViewIndex);
            ModernImageDecodeLimits limits;
            limits.maximumDecodedBytes = std::min(limits.maximumDecodedBytes,
                MaximumMeshImageBytes - decodedImageBytes);
            auto decoded = decodeModernImage(std::span<const std::byte>(bytes.data(), bytes.size()),
                encoding, limits);
            if (!decoded) return std::unexpected(decoded.error());
            result.image = *decoded;
            decodedImageBytes += result.image->rgba.size();
            images.emplace(imageIndex, result.image);
            return result;
        }

        [[nodiscard]] std::expected<MeshMaterial, MeshRuntimeError> materialFor(
            const fastgltf::Asset& asset, const fastgltf::Primitive& primitive,
            ImageCache& images, std::size_t& decodedImageBytes)
        {
            auto result = materialFactors(asset, primitive);
            if (!primitive.materialIndex) return result;
            const auto& material = asset.materials[*primitive.materialIndex];
            if (material.alphaMode == fastgltf::AlphaMode::Blend)
                return std::unexpected(error(MeshRuntimeErrorCode::ModernAssetUnsupported,
                    "modern GLB alpha blending requires transparent sorting and pipelines"));
            result.alphaMode = material.alphaMode == fastgltf::AlphaMode::Mask ?
                ModernAlphaMode::Mask : ModernAlphaMode::Opaque;
            result.alphaCutoff = static_cast<float>(material.alphaCutoff);
            if (!std::isfinite(result.alphaCutoff) || result.alphaCutoff < 0.0F)
                return std::unexpected(error(MeshRuntimeErrorCode::ModernAssetInvalid,
                    "modern GLB alpha cutoff must be finite and nonnegative"));
            const auto bind = [&](const auto& source, std::optional<ModernTextureBinding>& target,
                                  ModernTextureColorSpace colorSpace, float scale = 1.0F)
                -> std::expected<void, MeshRuntimeError>
            {
                if (!source) return {};
                auto binding = textureBindingFor(asset, *source, colorSpace, scale, images, decodedImageBytes);
                if (!binding) return std::unexpected(binding.error());
                target = std::move(*binding);
                return {};
            };
            if (auto bound = bind(material.pbrData.baseColorTexture, result.baseColorTexture,
                    ModernTextureColorSpace::Srgb); !bound) return std::unexpected(bound.error());
            if (auto bound = bind(material.pbrData.metallicRoughnessTexture, result.metallicRoughnessTexture,
                    ModernTextureColorSpace::Linear); !bound) return std::unexpected(bound.error());
            if (auto bound = bind(material.normalTexture, result.normalTexture, ModernTextureColorSpace::Linear,
                    material.normalTexture ? static_cast<float>(material.normalTexture->scale) : 1.0F);
                !bound) return std::unexpected(bound.error());
            if (auto bound = bind(material.emissiveTexture, result.emissiveTexture,
                    ModernTextureColorSpace::Srgb); !bound) return std::unexpected(bound.error());
            if (auto bound = bind(material.occlusionTexture, result.occlusionTexture, ModernTextureColorSpace::Linear,
                    material.occlusionTexture ? static_cast<float>(material.occlusionTexture->strength) : 1.0F);
                !bound) return std::unexpected(bound.error());
            if (result.occlusionTexture &&
                (result.occlusionTexture->scale < 0.0F || result.occlusionTexture->scale > 1.0F))
                return std::unexpected(error(MeshRuntimeErrorCode::ModernAssetInvalid,
                    "modern GLB occlusion strength must be between zero and one"));
            if ((result.baseColorTexture || result.metallicRoughnessTexture || result.normalTexture ||
                 result.emissiveTexture || result.occlusionTexture) &&
                primitive.findAttribute("TEXCOORD_0") == primitive.attributes.end())
                return std::unexpected(error(MeshRuntimeErrorCode::ModernAssetInvalid,
                    "modern GLB textured primitive requires TEXCOORD_0"));
            return result;
        }
    }


    std::expected<
        std::shared_ptr<const MeshRenderData>,
        MeshRuntimeError>
    loadModernGltfMesh(
        const std::filesystem::path& path,
        ModernGltfLoadOptions options) try
    {
        if (path.empty() || options.unitsPerMeter <= 0.0F ||
            !std::isfinite(options.unitsPerMeter) ||
            !std::isfinite(options.yawDegrees) ||
            !std::ranges::all_of(options.localOffset,
                [](float value) { return std::isfinite(value); }))
            return std::unexpected(error(
                MeshRuntimeErrorCode::ModernAssetInvalid,
                "invalid modern GLB path or world scale"));

        // GltfFileStream's constructor calls throwing file_size before isOpen.
        // Validate missing/unreadable paths and cap parser input before opening.
        std::error_code fileError;
        const bool regular = std::filesystem::is_regular_file(path, fileError);
        const auto inputBytes = regular && !fileError
            ? std::filesystem::file_size(path, fileError) : 0;
        if (!regular || fileError)
            return std::unexpected(error(MeshRuntimeErrorCode::SourceLoadFailed,
                "modern GLB file is unavailable: " + path.string()));
        constexpr std::uintmax_t MaxGlbInputBytes = 256U * 1024U * 1024U;
        if (inputBytes > MaxGlbInputBytes)
            return std::unexpected(error(MeshRuntimeErrorCode::ModernAssetInvalid,
                "modern GLB input exceeds the 256 MiB parser budget"));

        fastgltf::GltfFileStream stream(path);
        if (!stream.isOpen())
            return std::unexpected(error(
                MeshRuntimeErrorCode::SourceLoadFailed,
                "modern GLB file cannot be opened: " +
                    path.string()));

        fastgltf::Parser parser(fastgltf::Extensions::KHR_texture_transform |
            fastgltf::Extensions::KHR_materials_emissive_strength);
        auto loaded = parser.loadGltfBinary(
            stream,
            path.parent_path(),
            fastgltf::Options::None);
        if (loaded.error() != fastgltf::Error::None ||
            loaded.get_if() == nullptr)
            return std::unexpected(error(
                MeshRuntimeErrorCode::ModernAssetInvalid,
                std::string("fastgltf: ") +
                    std::string(fastgltf::getErrorMessage(
                        loaded.error()))));

        auto asset = std::move(loaded.get());
        if (!asset.animations.empty())
            return std::unexpected(error(MeshRuntimeErrorCode::ModernAssetUnsupported,
                "modern GLB animations require the animation adapter"));
        // Sparse reference/byte checks alone do not validate ordered, in-range
        // sparse indices. Keep the static bridge explicit until that semantic
        // validation is supported before accessor decoding.
        if (std::ranges::any_of(asset.accessors,
                [](const auto& accessor) { return accessor.sparse.has_value(); }))
            return std::unexpected(error(MeshRuntimeErrorCode::ModernAssetUnsupported,
                "modern GLB sparse accessors require bounded semantic validation"));
        if (!validHierarchy(asset))
            return std::unexpected(error(
                MeshRuntimeErrorCode::ModernAssetInvalid,
                "modern GLB node hierarchy is invalid or too deep"));
        if (asset.scenes.empty() || asset.nodes.empty())
            return std::unexpected(error(
                MeshRuntimeErrorCode::NoRenderableGeometry,
                "modern GLB has no scene nodes"));

        auto result = std::make_shared<MeshRenderData>();
        bool boundsInitialized = false;
        bool failed = false;
        MeshRuntimeError failure{};
        ImageCache images;
        std::size_t decodedImageBytes{};

        const auto sceneIndex =
            asset.defaultScene.value_or(std::size_t{0});
        if (sceneIndex >= asset.scenes.size())
            return std::unexpected(error(
                MeshRuntimeErrorCode::ModernAssetInvalid,
                "modern GLB default scene is out of range"));

        const auto rotation = yawRotation(options.yawDegrees);
        fastgltf::iterateSceneNodes(
            asset,
            sceneIndex,
            fastgltf::math::fmat4x4(1.0F),
            [&](fastgltf::Node& node,
                const fastgltf::math::fmat4x4& world)
            {
                if (failed || !node.meshIndex)
                    return;
                if (node.skinIndex)
                {
                    failed = true;
                    failure = error(MeshRuntimeErrorCode::ModernAssetUnsupported,
                        "modern GLB skins require the animation pipeline");
                    return;
                }
                if (*node.meshIndex >= asset.meshes.size())
                {
                    failed = true;
                    failure = error(
                        MeshRuntimeErrorCode::ModernAssetInvalid,
                        "modern GLB node references an invalid mesh");
                    return;
                }

                const auto& mesh = asset.meshes[*node.meshIndex];
                // Constant for this node: retain the original vertex arithmetic
                // and singular/nonfinite guards, but avoid millions of inverses.
                const auto linear = fastgltf::math::fmat3x3(world);
                const auto normalMatrix = fastgltf::math::transpose(
                    fastgltf::math::inverse(linear));
                const auto determinant = fastgltf::math::determinant(linear);
                for (const auto& primitive : mesh.primitives)
                {
                    if (failed) return;
                    if (primitive.materialIndex &&
                        *primitive.materialIndex >= asset.materials.size())
                    {
                        failed = true;
                        failure = error(MeshRuntimeErrorCode::ModernAssetInvalid,
                            "modern GLB material reference is out of range");
                        return;
                    }
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
                    auto material = materialFor(asset, primitive, images, decodedImageBytes);
                    if (!material)
                    {
                        failed = true;
                        failure = std::move(material.error());
                        return;
                    }

                    const auto position = primitive.findAttribute("POSITION");
                    const auto normal = primitive.findAttribute("NORMAL");
                    if (position == primitive.attributes.end() ||
                        normal == primitive.attributes.end() ||
                        !validAccessor(asset, position->accessorIndex,
                            fastgltf::AccessorType::Vec3) ||
                        !validAccessor(asset, normal->accessorIndex,
                            fastgltf::AccessorType::Vec3))
                    {
                        failed = true;
                        failure = error(
                            MeshRuntimeErrorCode::ModernAssetInvalid,
                            "modern GLB primitive requires valid POSITION and NORMAL accessors");
                        return;
                    }

                    const auto& positionAccessor =
                        asset.accessors[position->accessorIndex];
                    const auto& normalAccessor =
                        asset.accessors[normal->accessorIndex];
                    if (positionAccessor.count != normalAccessor.count ||
                        positionAccessor.type != fastgltf::AccessorType::Vec3 ||
                        normalAccessor.type != fastgltf::AccessorType::Vec3)
                    {
                        failed = true;
                        failure = error(
                            MeshRuntimeErrorCode::ModernAssetInvalid,
                            "modern GLB position/normal counts differ");
                        return;
                    }

                    if (positionAccessor.count > options.limits.maximumVertices ||
                        result->vertices.size() >
                            options.limits.maximumVertices -
                            positionAccessor.count ||
                        positionAccessor.count >
                            std::numeric_limits<std::uint32_t>::max() -
                                result->vertices.size())
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
                    const auto tangent = primitive.findAttribute("TANGENT");
                    if (tangent != primitive.attributes.end() &&
                        (!validAccessor(asset, tangent->accessorIndex, fastgltf::AccessorType::Vec4) ||
                         asset.accessors[tangent->accessorIndex].count != positionAccessor.count))
                    {
                        failed = true;
                        failure = error(MeshRuntimeErrorCode::ModernAssetInvalid,
                            "modern GLB tangent accessor must match the vertex count");
                        return;
                    }
                    if (uv != primitive.attributes.end() &&
                        (!validAccessor(asset, uv->accessorIndex,
                            fastgltf::AccessorType::Vec2) ||
                         asset.accessors[uv->accessorIndex].count !=
                            positionAccessor.count))
                    {
                        failed = true;
                        failure = error(MeshRuntimeErrorCode::ModernAssetInvalid,
                            "modern GLB UV accessor must match the vertex count");
                        return;
                    }
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
                            world,
                            sourcePosition,
                            options.unitsPerMeter,
                            rotation);
                        const auto worldNormal = transformNormal(
                            normalMatrix,
                            sourceNormal,
                            rotation);
                        for (std::size_t axis = 0; axis < 3; ++axis)
                        {
                            if (!std::isfinite(worldPosition[axis]) ||
                                !std::isfinite(worldNormal[axis]))
                            {
                                failed = true;
                                failure = error(MeshRuntimeErrorCode::ModernAssetInvalid,
                                    "modern GLB transform or normal is nonfinite or singular");
                                return;
                            }
                        }

                        auto& vertex = result->vertices[baseVertex + index];
                        vertex.position = {
                            worldPosition[0],
                            worldPosition[1],
                            worldPosition[2]};
                        vertex.normal = {
                            worldNormal[0],
                            worldNormal[1],
                            worldNormal[2]};

                        if (tangent != primitive.attributes.end())
                        {
                            const auto sourceTangent = fastgltf::getAccessorElement<fastgltf::math::fvec4>(
                                asset, asset.accessors[tangent->accessorIndex], index);
                            auto direction = rotateAroundY(linear * fastgltf::math::fvec3(
                                sourceTangent[0], sourceTangent[1], sourceTangent[2]), rotation);
                            direction -= worldNormal * fastgltf::math::dot(worldNormal, direction);
                            const auto length = fastgltf::math::length(direction);
                            if (!std::isfinite(length) || length <= 0.0F ||
                                !std::isfinite(determinant) || determinant == 0.0F ||
                                (sourceTangent[3] != -1.0F && sourceTangent[3] != 1.0F))
                            {
                                failed = true;
                                failure = error(MeshRuntimeErrorCode::ModernAssetInvalid,
                                    "modern GLB authored tangent is invalid or singular");
                                return;
                            }
                            direction /= length;
                            vertex.tangent = {direction[0], direction[1], direction[2],
                                sourceTangent[3] * (determinant < 0.0F ? -1.0F : 1.0F)};
                        }

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
                            if (!std::isfinite(sourceUv[0]) || !std::isfinite(sourceUv[1]))
                            {
                                failed = true;
                                failure = error(MeshRuntimeErrorCode::ModernAssetInvalid,
                                    "modern GLB UV coordinates are nonfinite");
                                return;
                            }
                        }
                        extendBounds(
                            result->bounds,
                            vertex,
                            boundsInitialized);
                    }

                    const fastgltf::Accessor* indexAccessor = nullptr;
                    if (primitive.indicesAccessor)
                    {
                        if (!validAccessor(asset, *primitive.indicesAccessor,
                            fastgltf::AccessorType::Scalar))
                        {
                            failed = true;
                            failure = error(MeshRuntimeErrorCode::ModernAssetInvalid,
                                "modern GLB index accessor is invalid");
                            return;
                        }
                        indexAccessor = &asset.accessors[*primitive.indicesAccessor];
                        if (indexAccessor->normalized ||
                            (indexAccessor->componentType != fastgltf::ComponentType::UnsignedByte &&
                             indexAccessor->componentType != fastgltf::ComponentType::UnsignedShort &&
                             indexAccessor->componentType != fastgltf::ComponentType::UnsignedInt))
                        {
                            failed = true;
                            failure = error(MeshRuntimeErrorCode::ModernAssetInvalid,
                                "modern GLB indices must be unsigned integers");
                            return;
                        }
                    }
                    const auto indexCount = indexAccessor ? indexAccessor->count : positionAccessor.count;
                    if (indexCount % 3 != 0)
                    {
                        failed = true;
                        failure = error(MeshRuntimeErrorCode::ModernAssetInvalid,
                            "modern GLB triangle indices must be scalar triplets");
                        return;
                    }
                    if (indexCount > options.limits.maximumIndices ||
                        result->indices.size() >
                            options.limits.maximumIndices -
                            indexCount)
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
                        firstIndex + indexCount);
                    for (std::size_t index = 0;
                        index < indexCount;
                        ++index)
                    {
                        const auto sourceIndex =
                            indexAccessor ? fastgltf::getAccessorElement<std::uint32_t>(
                                asset, *indexAccessor, index) : static_cast<std::uint32_t>(index);
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
                        indexCount,
                        std::move(*material),
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

        std::array<float, 3> offset = options.localOffset;
        if (options.groundToZero)
            offset[1] -= result->bounds.minimum[1];
        for (std::size_t axis = 0; axis < 3; ++axis)
            if (!std::isfinite(offset[axis]) ||
                !std::isfinite(result->bounds.minimum[axis] + offset[axis]) ||
                !std::isfinite(result->bounds.maximum[axis] + offset[axis]))
                return std::unexpected(error(MeshRuntimeErrorCode::ModernAssetInvalid,
                    "modern GLB offset exceeds finite world coordinates"));

        if (offset != std::array<float, 3>{})
        {
            for (auto& vertex : result->vertices)
                for (std::size_t axis = 0; axis < 3; ++axis)
                    vertex.position[axis] += offset[axis];
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                result->bounds.minimum[axis] += offset[axis];
                result->bounds.maximum[axis] += offset[axis];
            }
        }

        return std::const_pointer_cast<const MeshRenderData>(result);
    }
    catch (const std::filesystem::filesystem_error& failure)
    {
        return std::unexpected(error(MeshRuntimeErrorCode::SourceLoadFailed,
            "modern GLB file access failed: " + std::string(failure.what())));
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error(MeshRuntimeErrorCode::ModernAssetInvalid,
            "modern GLB allocation failed"));
    }
    catch (const std::length_error&)
    {
        return std::unexpected(error(MeshRuntimeErrorCode::ModernAssetInvalid,
            "modern GLB allocation exceeds container limits"));
    }
}
