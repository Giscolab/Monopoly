#include "ModernGltfMesh.hpp"
#include "ModernImageDecoder.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using namespace monopoly::data;
    int failures{};
    void expect(bool condition, std::string_view description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << description << '\n';
        if (!condition) ++failures;
    }

    void word(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }

    std::string triangleJson()
    {
        return R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}]}],"buffers":[{"byteLength":108}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":12}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5125,"count":3,"type":"SCALAR"}]})";
    }

    void replace(std::string& json, std::string_view from, std::string_view to)
    {
        const auto offset = json.find(from);
        if (offset == std::string::npos) throw std::runtime_error("fixture replacement missing");
        json.replace(offset, from.size(), to);
    }

    std::vector<std::uint8_t> redPng()
    {
        // Independently generated 1x1 red RGBA PNG, zlib-compressed scanline.
        return {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,
            8,6,0,0,0,31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,248,207,192,
            240,31,0,5,0,1,255,137,153,61,29,0,0,0,0,73,69,78,68,174,66,96,130};
    }

    std::vector<std::uint8_t> redJpeg()
    {
        // Independent Windows GDI+ 1x1 RGB red JPEG fixture.
        constexpr std::string_view encoded =
            "/9j/4AAQSkZJRgABAQEAYABgAAD/2wBDAAMCAgMCAgMDAwMEAwMEBQgFBQQEBQoHBwYIDAoMDAsKCwsNDhIQDQ4RDgsLEBYQERMUFRUVDA8XGBYUGBIUFRT/2wBDAQMEBAUEBQkFBQkUDQsNFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBT/wAARCAABAAEDASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwD50ooor8MP9Uz/2Q==";
        constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::vector<std::uint8_t> result;
        std::uint32_t accumulator{};
        unsigned available{};
        for (char character : encoded)
        {
            if (character == '=') break;
            const auto value = alphabet.find(character);
            if (value == std::string_view::npos) throw std::runtime_error("invalid JPEG fixture encoding");
            accumulator = (accumulator << 6) | static_cast<std::uint32_t>(value);
            available += 6;
            if (available >= 8)
            {
                available -= 8;
                result.push_back(static_cast<std::uint8_t>(accumulator >> available));
            }
        }
        return result;
    }

    std::string texturedJson(std::size_t imageBytes)
    {
        auto json = triangleJson();
        replace(json, "\"byteLength\":108", "\"byteLength\":" + std::to_string(108 + imageBytes));
        replace(json, "\"indices\":3", "\"indices\":3,\"material\":0");
        replace(json, "\"byteOffset\":96,\"byteLength\":12}]",
            "\"byteOffset\":96,\"byteLength\":12},{\"buffer\":0,\"byteOffset\":108,\"byteLength\":" +
            std::to_string(imageBytes) + "}]");
        json.pop_back();
        json += R"(,"images":[{"bufferView":4,"mimeType":"image/png"}],"textures":[{"source":0,"sampler":0}],"samplers":[{"wrapS":33071,"wrapT":33648,"minFilter":9987,"magFilter":9728}],"materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":0}},"normalTexture":{"index":0,"scale":0.5},"emissiveTexture":{"index":0},"occlusionTexture":{"index":0,"strength":0.25}}]})";
        return json;
    }

    struct Fixture
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("monopoly-modern-gltf-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) + ".glb");
        ~Fixture() { std::error_code ignored; std::filesystem::remove(path, ignored); }

        void write(std::string json, const std::vector<std::uint8_t>& tail = {})
        {
            while (json.size() % 4) json.push_back(' ');
            std::vector<std::uint8_t> binary;
            for (float value : {0.F, 0.F, 0.F, 1.F, 0.F, 0.F, 0.F, 1.F, 0.F,
                               0.F, 0.F, 1.F, 0.F, 0.F, 1.F, 0.F, 0.F, 1.F,
                               0.F, 0.F, 1.F, 0.F, 0.F, 1.F})
                word(binary, std::bit_cast<std::uint32_t>(value));
            for (std::uint32_t index : {0U, 1U, 2U}) word(binary, index);
            binary.insert(binary.end(), tail.begin(), tail.end());
            while (binary.size() % 4) binary.push_back(0);
            std::vector<std::uint8_t> bytes;
            word(bytes, 0x46546c67);
            word(bytes, 2);
            word(bytes, static_cast<std::uint32_t>(28 + json.size() + binary.size()));
            word(bytes, static_cast<std::uint32_t>(json.size()));
            word(bytes, 0x4e4f534a);
            bytes.insert(bytes.end(), json.begin(), json.end());
            word(bytes, static_cast<std::uint32_t>(binary.size()));
            word(bytes, 0x004e4942);
            bytes.insert(bytes.end(), binary.begin(), binary.end());
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            if (!output) throw std::runtime_error("cannot write GLB fixture");
        }
    };

    void rejected(Fixture& fixture, std::string json, std::string_view message,
                  MeshRuntimeErrorCode code = MeshRuntimeErrorCode::ModernAssetInvalid)
    {
        fixture.write(std::move(json));
        const auto result = loadModernGltfMesh(fixture.path);
        expect(!result && result.error().code == code, message);
    }
}

namespace
{
    // Optional qualification of the actual factor-only city corpus. Hash every
    // decoded scalar explicitly; no struct padding or pointer values participate.
    int citySignatures(const std::filesystem::path& directory)
    {
        using namespace monopoly::data;
        std::vector<std::filesystem::path> paths;
        for (const auto& entry : std::filesystem::directory_iterator(directory))
            if (entry.is_regular_file() && entry.path().extension() == ".glb" &&
                entry.path().filename().string().starts_with("procedural_city_"))
                paths.push_back(entry.path());
        std::sort(paths.begin(), paths.end());
        if (paths.size() != 17) throw std::runtime_error("expected seventeen real city groups");
        constexpr std::uint64_t Basis = 14695981039346656037ULL;
        const auto mix = [](std::uint64_t& hash, std::uint64_t word)
        { hash = (hash ^ word) * 1099511628211ULL; };
        const auto scalar = [&](std::uint64_t& hash, float value)
        { mix(hash, std::bit_cast<std::uint32_t>(value)); };
        for (const auto& path : paths)
        {
            ModernGltfLoadOptions options;
            options.unitsPerMeter = 200.F;
            options.groundToZero = false;
            const auto begin = std::chrono::steady_clock::now();
            auto loaded = loadModernGltfMesh(path, options);
            const auto seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - begin).count();
            if (!loaded) throw std::runtime_error(loaded.error().detail);
            const auto& mesh = **loaded;
            auto vertices = Basis, indices = Basis, materials = Basis, bounds = Basis;
            mix(vertices, mesh.vertices.size());
            for (const auto& vertex : mesh.vertices)
            {
                for (float value : vertex.position) scalar(vertices, value);
                for (float value : vertex.normal) scalar(vertices, value);
                for (float value : vertex.uv) scalar(vertices, value);
                for (float value : vertex.tangent) scalar(vertices, value);
            }
            mix(indices, mesh.indices.size());
            for (auto value : mesh.indices) mix(indices, value);
            mix(materials, mesh.batches.size());
            for (const auto& batch : mesh.batches)
            {
                const auto& material = batch.material;
                if (batch.texture || material.baseColorTexture || material.metallicRoughnessTexture ||
                    material.normalTexture || material.emissiveTexture || material.occlusionTexture)
                    throw std::runtime_error("city signature mode requires factor-only assets");
                mix(materials, batch.firstIndex); mix(materials, batch.indexCount);
                mix(materials, static_cast<unsigned>(material.model)); mix(materials, material.rawDiffuse);
                for (float value : material.diffuse) scalar(materials, value);
                scalar(materials, material.metallic); scalar(materials, material.roughness);
                for (float value : material.emissive) scalar(materials, value);
                scalar(materials, material.emissiveStrength); mix(materials, material.doubleSided);
                mix(materials, static_cast<unsigned>(material.alphaMode)); scalar(materials, material.alphaCutoff);
            }
            for (float value : mesh.bounds.minimum) scalar(bounds, value);
            for (float value : mesh.bounds.maximum) scalar(bounds, value);
            std::cout.precision(9);
            std::cout << path.filename().string() << '\t' << seconds << '\t' << mesh.vertices.size()
                << '\t' << std::hex << vertices << '\t' << indices << '\t' << materials
                << '\t' << bounds << std::dec << '\n';
        }
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--city-signatures")
        return citySignatures(argv[2]);
    if (argc != 1) return 2;
    Fixture fixture;
    fixture.write(triangleJson());
    auto result = loadModernGltfMesh(fixture.path);
    expect(result && (*result)->vertices.size() == 3 && (*result)->indices.size() == 3,
        "valid embedded indexed triangle loads");

    ModernGltfLoadOptions options;
    options.limits.maximumVertices = 2;
    result = loadModernGltfMesh(fixture.path, options);
    expect(!result && result.error().code == MeshRuntimeErrorCode::VertexLimitExceeded,
        "first primitive cannot exceed vertex budget");
    options = {};
    options.limits.maximumIndices = 2;
    result = loadModernGltfMesh(fixture.path, options);
    expect(!result && result.error().code == MeshRuntimeErrorCode::IndexLimitExceeded,
        "first primitive cannot exceed index budget");
    options = {};
    options.yawDegrees = std::numeric_limits<float>::quiet_NaN();
    result = loadModernGltfMesh(fixture.path, options);
    expect(!result && result.error().code == MeshRuntimeErrorCode::ModernAssetInvalid,
        "nonfinite rotation option is rejected");
    options = {};
    options.localOffset[0] = std::numeric_limits<float>::infinity();
    result = loadModernGltfMesh(fixture.path, options);
    expect(!result && result.error().code == MeshRuntimeErrorCode::ModernAssetInvalid,
        "nonfinite offset option is rejected");

    auto json = triangleJson();
    replace(json, ",\"indices\":3", "");
    fixture.write(json);
    result = loadModernGltfMesh(fixture.path);
    expect(result && (*result)->indices == std::vector<std::uint32_t>{0, 1, 2},
        "unindexed triangle receives sequential indices after validation");

    json = triangleJson();
    replace(json, "\"POSITION\":0", "\"POSITION\":99");
    replace(json, ",\"indices\":3", "");
    rejected(fixture, json, "unindexed invalid POSITION reference fails without parser index generation");
    json = triangleJson();
    replace(json, "\"NORMAL\":1", "\"NORMAL\":99");
    rejected(fixture, json, "invalid NORMAL accessor reference fails safely");
    json = triangleJson();
    replace(json, "\"TEXCOORD_0\":2", "\"TEXCOORD_0\":99");
    rejected(fixture, json, "invalid UV accessor reference fails safely");
    json = triangleJson();
    replace(json, "\"indices\":3", "\"indices\":99");
    rejected(fixture, json, "invalid index accessor reference fails safely");
    json = triangleJson();
    replace(json, "\"bufferView\":2,\"componentType\":5126,\"count\":3",
        "\"bufferView\":2,\"componentType\":5126,\"count\":2");
    rejected(fixture, json, "short UV accessor cannot be read past its count");
    json = triangleJson();
    replace(json, "\"byteLength\":36", "\"byteLength\":4");
    rejected(fixture, json, "truncated position view fails before decoding");
    json = triangleJson();
    replace(json, "\"byteOffset\":0", "\"byteOffset\":4096");
    rejected(fixture, json, "buffer view outside embedded payload fails safely");
    json = triangleJson();
    replace(json, "\"bufferView\":0", "\"bufferView\":99");
    rejected(fixture, json, "invalid buffer view reference fails safely");
    json = triangleJson();
    replace(json, "\"componentType\":5125,\"count\":3", "\"componentType\":5125,\"count\":2");
    rejected(fixture, json, "incomplete index triangle fails");
    json = triangleJson();
    replace(json, "\"componentType\":5125", "\"componentType\":5126");
    rejected(fixture, json, "floating point index accessor fails");
    json = triangleJson();
    replace(json, "\"mesh\":0", "\"mesh\":0,\"scale\":[0,0,0]");
    rejected(fixture, json, "singular node transform cannot produce invalid normals");
    json = triangleJson();
    replace(json, "\"mesh\":0", "\"mesh\":0,\"children\":[0]");
    rejected(fixture, json, "cyclic hierarchy fails before recursive traversal");
    json = triangleJson();
    replace(json, "\"mesh\":0", "\"mesh\":0,\"children\":[99]");
    rejected(fixture, json, "invalid child node fails before recursive traversal");
    json = triangleJson();
    replace(json, "\"nodes\":[0]", "\"nodes\":[99]");
    rejected(fixture, json, "invalid scene root fails safely");
    json = triangleJson();
    replace(json, "\"indices\":3", "\"indices\":3,\"material\":99");
    rejected(fixture, json, "invalid material reference does not silently use defaults");
    json = triangleJson();
    replace(json, "\"mesh\":0", "\"mesh\":0,\"skin\":0");
    rejected(fixture, json, "skinned static mesh is explicitly unsupported",
        MeshRuntimeErrorCode::ModernAssetUnsupported);

    const auto png = redPng();
    const auto bytes = std::as_bytes(std::span(png));
    auto image = decodeModernImage(bytes, ModernImageEncoding::Png);
    expect(image && (*image)->width == 1 && (*image)->height == 1 &&
        (*image)->rgba == std::vector<std::uint8_t>{255,0,0,255},
        "embedded PNG decodes independent known RGBA pixels");
    expect(!decodeModernImage(bytes, ModernImageEncoding::Jpeg),
        "image MIME signature mismatch is rejected");
    ModernImageDecodeLimits imageLimits;
    imageLimits.maximumDecodedBytes = 3;
    expect(!decodeModernImage(bytes, ModernImageEncoding::Png, imageLimits),
        "decoded image byte budget checked before allocation");
    imageLimits = {};
    imageLimits.maximumEncodedBytes = png.size() - 1;
    expect(!decodeModernImage(bytes, ModernImageEncoding::Png, imageLimits),
        "encoded image byte budget checked before decoding");
    expect(!decodeModernImage(bytes.first(40), ModernImageEncoding::Png),
        "truncated PNG fails without publishing pixels");
    const auto jpeg = redJpeg();
    const auto jpegImage = decodeModernImage(std::as_bytes(std::span(jpeg)), ModernImageEncoding::Jpeg);
    expect(jpegImage && (*jpegImage)->width == 1 && (*jpegImage)->height == 1 &&
        (*jpegImage)->rgba.size() == 4 && (*jpegImage)->rgba[0] >= 250 &&
        (*jpegImage)->rgba[1] <= 5 && (*jpegImage)->rgba[2] <= 5 && (*jpegImage)->rgba[3] == 255,
        "embedded JPEG decodes independent red pixel with opaque alpha");
    auto jpegJson = texturedJson(jpeg.size());
    replace(jpegJson, "image/png", "image/jpeg");
    fixture.write(jpegJson, jpeg);
    result = loadModernGltfMesh(fixture.path);
    expect(result && (*result)->batches.front().material.baseColorTexture,
        "embedded JPEG GLB map loads with immutable pixel ownership");

    json = texturedJson(png.size());
    fixture.write(json, png);
    result = loadModernGltfMesh(fixture.path);
    expect(result.has_value(), "embedded five-map textured primitive loads");
    if (result)
    {
        const auto& material = (*result)->batches.front().material;
        expect(material.baseColorTexture && material.metallicRoughnessTexture &&
            material.normalTexture && material.emissiveTexture && material.occlusionTexture,
            "all five modern maps remain owned by material");
        if (material.baseColorTexture && material.normalTexture && material.emissiveTexture &&
            material.metallicRoughnessTexture && material.occlusionTexture)
        {
            expect(material.baseColorTexture->image == material.normalTexture->image &&
                material.baseColorTexture->image == material.emissiveTexture->image &&
                material.baseColorTexture->image == material.metallicRoughnessTexture->image &&
                material.baseColorTexture->image == material.occlusionTexture->image,
                "shared source image has one immutable pixel owner across map interpretations");
            expect(material.baseColorTexture->colorSpace == ModernTextureColorSpace::Srgb &&
                material.emissiveTexture->colorSpace == ModernTextureColorSpace::Srgb &&
                material.normalTexture->colorSpace == ModernTextureColorSpace::Linear &&
                material.metallicRoughnessTexture->colorSpace == ModernTextureColorSpace::Linear &&
                material.occlusionTexture->colorSpace == ModernTextureColorSpace::Linear,
                "color maps use sRGB and data maps use linear interpretation");
            expect(material.normalTexture->scale == 0.5F && material.occlusionTexture->scale == 0.25F,
                "normal scale and occlusion strength are preserved");
            const auto& sampler = material.baseColorTexture->sampler;
            expect(sampler.wrapS == ModernTextureWrap::ClampToEdge &&
                sampler.wrapT == ModernTextureWrap::MirroredRepeat &&
                sampler.minFilter == ModernTextureFilter::LinearMipmapLinear &&
                sampler.magFilter == ModernTextureFilter::Nearest,
                "glTF filter and wrap modes are preserved");
        }
        expect((*result)->vertices[0].tangent[3] == 0,
            "missing authored tangent remains marked for shader derivative basis");
    }

    const auto rejectTextured = [&](std::string altered, std::string_view message,
                                   MeshRuntimeErrorCode code = MeshRuntimeErrorCode::ModernAssetInvalid)
    {
        fixture.write(std::move(altered), png);
        const auto loaded = loadModernGltfMesh(fixture.path);
        expect(!loaded && loaded.error().code == code, message);
    };
    auto altered = json;
    replace(altered, "\"baseColorTexture\":{\"index\":0}",
        "\"baseColorTexture\":{\"index\":0,\"texCoord\":1}");
    rejectTextured(altered, "nonzero texture coordinate set is explicitly unsupported",
        MeshRuntimeErrorCode::ModernAssetUnsupported);
    altered = json;
    replace(altered, "\"baseColorTexture\":{\"index\":0}",
        "\"baseColorTexture\":{\"index\":0,\"extensions\":{\"KHR_texture_transform\":{\"offset\":[0.1,0]}}}");
    rejectTextured(altered, "texture transforms are explicitly unsupported",
        MeshRuntimeErrorCode::ModernAssetUnsupported);
    altered = json;
    replace(altered, "\"bufferView\":4,\"mimeType\":\"image/png\"", "\"uri\":\"outside.png\"");
    rejectTextured(altered, "external image path is explicitly unsupported",
        MeshRuntimeErrorCode::ModernAssetUnsupported);
    altered = json;
    replace(altered, "\"bufferView\":4", "\"bufferView\":99");
    rejectTextured(altered, "invalid image buffer view fails safely");
    altered = json;
    replace(altered, "\"source\":0", "\"source\":99");
    rejectTextured(altered, "invalid texture image reference fails safely");
    altered = json;
    replace(altered, "\"sampler\":0", "\"sampler\":99");
    rejectTextured(altered, "invalid texture sampler reference fails safely");
    altered = json;
    replace(altered, ",\"TEXCOORD_0\":2", "");
    rejectTextured(altered, "textured primitive requires matching texture coordinates");
    altered = json;
    replace(altered, "\"normalTexture\":", "\"alphaMode\":\"MASK\",\"alphaCutoff\":0.3,\"normalTexture\":");
    fixture.write(altered, png);
    result = loadModernGltfMesh(fixture.path);
    expect(result && (*result)->batches.front().material.alphaMode == ModernAlphaMode::Mask &&
        (*result)->batches.front().material.alphaCutoff == 0.3F,
        "glTF alpha mask and cutoff are preserved for shader discard");
    altered = json;
    replace(altered, "\"normalTexture\":", "\"alphaMode\":\"BLEND\",\"normalTexture\":");
    rejectTextured(altered, "alpha blending is explicitly unsupported until transparent draw sorting exists",
        MeshRuntimeErrorCode::ModernAssetUnsupported);
    altered = json;
    replace(altered, ",\"sampler\":0", "");
    fixture.write(altered, png);
    result = loadModernGltfMesh(fixture.path);
    expect(result && (*result)->batches.front().material.baseColorTexture &&
        (*result)->batches.front().material.baseColorTexture->sampler == ModernTextureSampler{},
        "omitted glTF sampler uses repeat wrapping and linear filtering");

    auto tangentJson = triangleJson();
    replace(tangentJson, "\"byteLength\":108", "\"byteLength\":156");
    replace(tangentJson, "\"TEXCOORD_0\":2", "\"TEXCOORD_0\":2,\"TANGENT\":4");
    replace(tangentJson, "\"byteOffset\":96,\"byteLength\":12}]",
        "\"byteOffset\":96,\"byteLength\":12},{\"buffer\":0,\"byteOffset\":108,\"byteLength\":48}]");
    replace(tangentJson, "\"type\":\"SCALAR\"}]",
        "\"type\":\"SCALAR\"},{\"bufferView\":4,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"}]");
    replace(tangentJson, "\"mesh\":0", "\"mesh\":0,\"scale\":[-2,3,1]");
    std::vector<std::uint8_t> tangents;
    for (int vertex = 0; vertex < 3; ++vertex)
        for (float value : {1.F, 0.F, 0.F, 1.F}) word(tangents, std::bit_cast<std::uint32_t>(value));
    fixture.write(tangentJson, tangents);
    result = loadModernGltfMesh(fixture.path);
    expect(result && (*result)->vertices[0].tangent == std::array<float,4>{-1.F,0.F,0.F,-1.F},
        "authored tangent follows node scale and reflection handedness");
    replace(tangentJson, "\"TANGENT\":4", "\"TANGENT\":99");
    fixture.write(tangentJson, tangents);
    result = loadModernGltfMesh(fixture.path);
    expect(!result && result.error().code == MeshRuntimeErrorCode::ModernAssetInvalid,
        "invalid authored tangent accessor fails before decoding");

    auto animatedJson = triangleJson();
    replace(animatedJson, "\"byteLength\":108", "\"byteLength\":120");
    replace(animatedJson, "\"byteOffset\":96,\"byteLength\":12}]",
        "\"byteOffset\":96,\"byteLength\":12},{\"buffer\":0,\"byteOffset\":108,\"byteLength\":12}]");
    replace(animatedJson, "\"type\":\"SCALAR\"}]",
        "\"type\":\"SCALAR\"},{\"bufferView\":4,\"componentType\":5126,\"count\":3,\"type\":\"SCALAR\",\"min\":[0],\"max\":[2]}]");
    animatedJson.pop_back();
    animatedJson += R"(,"animations":[{"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}],"samplers":[{"input":4,"output":0,"interpolation":"LINEAR"}]}]})";
    std::vector<std::uint8_t> times;
    for (float value : {0.F, 1.F, 2.F}) word(times, std::bit_cast<std::uint32_t>(value));
    fixture.write(animatedJson, times);
    result = loadModernGltfMesh(fixture.path);
    expect(!result && result.error().code == MeshRuntimeErrorCode::ModernAssetUnsupported,
        "animated GLB is explicitly unsupported rather than frozen at rest pose");

    auto sparseJson = triangleJson();
    replace(sparseJson, "\"bufferView\":0", R"("bufferView":0,"sparse":{"count":1,"indices":{"bufferView":3,"componentType":5125},"values":{"bufferView":0}})");
    rejected(fixture, sparseJson, "valid sparse accessor is explicitly unsupported before decoding",
        MeshRuntimeErrorCode::ModernAssetUnsupported);
    replace(sparseJson, "\"indices\":{\"bufferView\":3", "\"indices\":{\"bufferView\":99");
    rejected(fixture, sparseJson, "malformed sparse references cannot reach accessor decoding",
        MeshRuntimeErrorCode::ModernAssetUnsupported);

    result = loadModernGltfMesh(fixture.path.string() + ".missing");
    expect(!result && result.error().code == MeshRuntimeErrorCode::SourceLoadFailed,
        "missing modern GLB returns a recoverable file error before fastgltf construction");
    result = loadModernGltfMesh(fixture.path.parent_path());
    expect(!result && result.error().code == MeshRuntimeErrorCode::SourceLoadFailed,
        "directory modern GLB path returns a recoverable file error");
    std::filesystem::resize_file(fixture.path, 257ULL * 1024ULL * 1024ULL);
    result = loadModernGltfMesh(fixture.path);
    expect(!result && result.error().code == MeshRuntimeErrorCode::ModernAssetInvalid,
        "oversized modern GLB is rejected before parser input allocation");

    return failures == 0 ? 0 : 1;
}
