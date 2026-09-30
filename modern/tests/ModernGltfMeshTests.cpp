#include "ModernGltfMesh.hpp"

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

    struct Fixture
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("monopoly-modern-gltf-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) + ".glb");
        ~Fixture() { std::error_code ignored; std::filesystem::remove(path, ignored); }

        void write(std::string json)
        {
            while (json.size() % 4) json.push_back(' ');
            std::vector<std::uint8_t> binary;
            for (float value : {0.F, 0.F, 0.F, 1.F, 0.F, 0.F, 0.F, 1.F, 0.F,
                               0.F, 0.F, 1.F, 0.F, 0.F, 1.F, 0.F, 0.F, 1.F,
                               0.F, 0.F, 1.F, 0.F, 0.F, 1.F})
                word(binary, std::bit_cast<std::uint32_t>(value));
            for (std::uint32_t index : {0U, 1U, 2U}) word(binary, index);
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

int main()
{
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
    return failures == 0 ? 0 : 1;
}
