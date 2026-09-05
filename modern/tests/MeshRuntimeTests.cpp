#include "MeshRuntime.hpp"
#include "LegacyDataArchiveBuilder.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>

namespace
{
    using namespace monopoly::data;
    int failures{};

    void expect(bool condition, std::string_view description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << description << '\n';
        if (!condition) ++failures;
    }
    bool near(float left, float right)
    { return std::fabs(left - right) < 0.0001F; }
    void setWord(DataBytes& data, std::size_t index, std::uint32_t value)
    {
        for (unsigned i = 0; i < 4; ++i)
            data[index * 4 + i] = static_cast<std::byte>((value >> (i * 8U)) & 0xFFU);
    }
    DataBytes words(std::initializer_list<std::uint32_t> values)
    {
        DataBytes result(values.size() * 4);
        std::size_t index{};
        for (const auto value : values) setWord(result, index++, value);
        return result;
    }
    DataBytes flatTriangle()
    {
        return words({
            0x01020304, 0, 6, 2, 11, 0,
            1, 3, 0x80000011, 0x80000014, 0x8000001A,
            0xFFFFFFFF, 7, 0x80000001,
            0x00000008, 0x80010002, 0,
            0x00563412, 0, 0x00020001,
            0xFFEC000A, 30, 0x00280014, 60, 0x003C001E, 90,
            0xF8001000, 1024
        });
    }
    DataBytes texturedTriangle()
    {
        auto data = flatTriangle();
        data.resize(17 * 4);
        const auto tail = words({
            0x00002211, 0x12344433, 0x00006655,
            0, 0x00010001, 0x00020002,
            0xFFEC000A, 30, 0x00280014, 60, 0x003C001E, 90,
            4096, 0, 0x10000000, 0, 0, 4096
        });
        data.insert(data.end(), tail.begin(), tail.end());
        setWord(data, 9, 0x80000017);
        setWord(data, 10, 0x8000001D);
        setWord(data, 14, 0x0080000D);
        return data;
    }
    DataBytes texturedTriangleWithEmbeddedImage()
    {
        DataBytes data(179U * 4U);
        setWord(data, 0, 0x01020304); setWord(data, 1, 0);
        setWord(data, 2, 6); setWord(data, 3, 2);
        setWord(data, 4, 14); setWord(data, 5, 25);
        setWord(data, 6, 2);
        setWord(data, 7, 2);
        setWord(data, 8, 0x80000031); setWord(data, 9, 0x80000033);
        setWord(data, 10, 3);
        setWord(data, 11, 0x8000001F); setWord(data, 12, 0x80000025);
        setWord(data, 13, 0x8000002B);

        setWord(data, 14, 0xFFFFFFFF); setWord(data, 15, 7);
        setWord(data, 16, 0x80000001);
        setWord(data, 17, 0x02000001); setWord(data, 18, 0x80010007);
        setWord(data, 19, 0); setWord(data, 20, 0x00020002);
        setWord(data, 21, 0); setWord(data, 22, 0);
        setWord(data, 23, 0x00010100); setWord(data, 24, 0);

        setWord(data, 25, 0xFFFFFFFF); setWord(data, 26, 10);
        setWord(data, 27, 0x80000001);
        setWord(data, 28, 0x0080000D); setWord(data, 29, 0x80010002);
        setWord(data, 30, 0);
        setWord(data, 31, 0x00000000);
        setWord(data, 32, 0x00800003);
        setWord(data, 33, 0x00000100);
        setWord(data, 34, 0x00000000);
        setWord(data, 35, 0x00010001); setWord(data, 36, 0x00020002);
        setWord(data, 37, 0x0014000A); setWord(data, 38, 30);
        setWord(data, 39, 0x00140028); setWord(data, 40, 30);
        setWord(data, 41, 0x0014000A); setWord(data, 42, 60);
        setWord(data, 43, 0x00001000); setWord(data, 44, 0);
        setWord(data, 45, 0x10000000); setWord(data, 46, 0);
        setWord(data, 47, 0); setWord(data, 48, 4096);
        setWord(data, 49, 0x03020100); setWord(data, 50, 0x07060504);
        setWord(data, 51, 0x03E0001F); setWord(data, 52, 0x7FFF7C00);
        return data;
    }
    DataBytes animatedTriangle()
    {
        // One flat triangle plus one vertex and one normal MIMe diff block.
        // This follows the USE_OLD_FRAME hmdload.cpp layout used by Monopoly.
        DataBytes data(52U * 4U);
        setWord(data, 0, 0x01020304); setWord(data, 1, 0);
        setWord(data, 2, 5); setWord(data, 3, 1); setWord(data, 4, 11);
        setWord(data, 5, 1); setWord(data, 6, 4);
        setWord(data, 7, 0x80000017); // polygons at word 23
        setWord(data, 8, 0x8000001A); // vertices at word 26
        setWord(data, 9, 0x80000020); // normals at word 32
        setWord(data, 10, 0x80000022); // MIMe block base at word 34

        setWord(data, 11, 0xFFFFFFFF); setWord(data, 12, 6);
        setWord(data, 13, 0x80000003); // triangle + VtxMIMe + NrmMIMe
        setWord(data, 14, 0x00000008); setWord(data, 15, 0x80010002);
        setWord(data, 16, 0); // polygon offset relative to polygon base
        setWord(data, 17, 0x04010020); setWord(data, 18, 0x80010002);
        setWord(data, 19, 0); // vertex current block at MIMe base + 0
        setWord(data, 20, 0x04010021); setWord(data, 21, 0x80010002);
        setWord(data, 22, 3); // normal current block at MIMe base + 3

        setWord(data, 23, 0x00563412);
        setWord(data, 24, 0);
        setWord(data, 25, 0x00020001);
        setWord(data, 26, 0xFFEC000A); setWord(data, 27, 30);
        setWord(data, 28, 0x00280014); setWord(data, 29, 60);
        setWord(data, 30, 0x003C001E); setWord(data, 31, 90);
        setWord(data, 32, 0xF8001000); setWord(data, 33, 1024);

        setWord(data, 34, 0x00010000); // one vertex diff block
        setWord(data, 35, 0);
        setWord(data, 36, 6);          // vertex diff at word 40
        setWord(data, 37, 0x00000001); // one normal diff block
        setWord(data, 38, 0);
        setWord(data, 39, 14);         // normal diff at word 48

        setWord(data, 40, 0);          // affects all three HMD vertices
        setWord(data, 41, 0x00030000);
        setWord(data, 42, 0xFFFA0005); setWord(data, 43, 7);
        setWord(data, 44, 0); setWord(data, 45, 0);
        setWord(data, 46, 0x0006FFFB); setWord(data, 47, 0x0000FFF9);

        setWord(data, 48, 0);          // affects HMD normal zero
        setWord(data, 49, 0x00010000);
        setWord(data, 50, 0xFFFD0002); setWord(data, 51, 4);
        return data;
    }

    std::shared_ptr<const LegacyMeshData> parse(DataBytes bytes)
    {
        auto mesh = LegacyMeshData::parse(
            std::make_shared<const DataBytes>(std::move(bytes)));
        expect(mesh.has_value(), "synthetic immutable HMD parses before MESHX build");
        return mesh ? std::make_shared<const LegacyMeshData>(std::move(*mesh)) : nullptr;
    }

    struct ResourceFixture
    {
        std::filesystem::path root;
        ResourceFixture()
        {
            root = std::filesystem::current_path() / ("MeshRuntimeResources-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(root / "Dat_Mon");
        }
        ~ResourceFixture()
        {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }
    };
    bool writeResources(const std::filesystem::path& root, const DataBytes& hmd)
    {
        const std::array names{"dat_main.dat", "dat_pat.dat", "dat_bord.dat",
            "dat_brd2.dat", "dat_3d.dat", "dat_ln01.dat", "dat_lm01.dat", "dat_lk01.dat"};
        for (std::size_t index = 0; index < names.size(); ++index)
        {
            std::vector<ArchiveBuildItem> items;
            if (index == 4) items.push_back({LegacyDataType::Hmd, hmd});
            else if (index == 5)
            {
                items.push_back({LegacyDataType::IndexTable,
                    {std::byte{42}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}, std::byte{0}}});
                items.push_back({LegacyDataType::String,
                    {std::byte{'A'}, std::byte{0}, std::byte{0}, std::byte{0}}});
            }
            else items.push_back({LegacyDataType::Native, {std::byte{1}}});
            if (!writeLegacyDataArchive(root / "Dat_Mon" / names[index], items)) return false;
        }
        return true;
    }

    void testUntexturedMeshAndRenderData()
    {
        auto source = parse(flatTriangle());
        auto built = MeshXRuntime::build(source);
        expect(built.has_value(), "MESHX runtime builds from supported HMD triangle data");
        if (!built) return;
        source.reset();
        expect(built->source() && built->source()->bytes().size() == 112,
            "MESHX owns its immutable HMD source after caller release");
        expect(built->vertices().size() == 3 && built->groups().size() == 1 &&
            built->groups()[0].indices.size() == 3,
            "MESHX contains deduplicated geometry and one material group");
        const auto& first = built->vertices()[0];
        expect(near(first.position[0], 10) && near(first.position[1], 20) &&
            near(first.position[2], 30) && near(first.normal[0], 1) &&
            near(first.normal[1], .5F) && near(first.normal[2], .25F),
            "MESHX applies the historical Y-axis conversion and 12-bit fixed normals");
        expect(near(first.uv[0], -1) && near(first.uv[1], -1) &&
            built->groups()[0].material.rawDiffuse == 0x00563412 &&
            near(built->groups()[0].material.diffuse[0], 0x12 / 255.0F) &&
            near(built->groups()[0].material.diffuse[2], 0x56 / 255.0F),
            "untextured UV sentinel and byte-ordered diffuse material are retained");
        expect(near(built->bounds().minimum[0], 10) &&
            near(built->bounds().maximum[0], 30) &&
            near(built->bounds().minimum[1], -60) &&
            near(built->bounds().maximum[1], 20),
            "MESHX computes CPU bounds from converted vertices");

        const auto render = makeMeshRenderData(*built);
        expect(render.vertices.size() == 3 && render.indices.size() == 3 &&
            render.batches.size() == 1 && render.batches[0].firstIndex == 0 &&
            render.batches[0].indexCount == 3 && !render.batches[0].texture,
            "renderer-independent data flattens MESHX groups into an indexed batch");
    }

    void testMimePoseEvaluation()
    {
        auto source = parse(animatedTriangle());
        auto built = MeshXRuntime::build(source);
        expect(built && built->poseCount() == 2,
            "MESHX exposes implicit base pose plus one decoded MIMe pose");
        if (!built) return;

        auto base = built->evaluatePose(0, 0, 0.75F);
        expect(base && base->vertices.size() == 3 &&
            near(base->vertices[0].position[0], 10.0F) &&
            near(base->vertices[0].normal[0], 1.0F),
            "same-pose shortcut keeps undeformed pose zero regardless of amount");

        auto pose = built->evaluatePose(1, 1, -4.0F);
        expect(pose && near(pose->vertices[0].position[0], 15.0F) &&
            near(pose->vertices[0].position[1], 26.0F) &&
            near(pose->vertices[0].position[2], 37.0F) &&
            near(pose->vertices[2].position[0], 25.0F) &&
            near(pose->vertices[2].position[1], -66.0F) &&
            near(pose->vertices[2].position[2], 83.0F),
            "vertex MIMe pose is base plus signed delta with historical Y inversion");
        expect(pose && near(pose->vertices[0].normal[0], 3.0F) &&
            near(pose->vertices[0].normal[1], 3.5F) &&
            near(pose->vertices[0].normal[2], 4.25F),
            "USE_OLD_FRAME normal MIMe adds raw SVECTOR deltas after base normal conversion");
        expect(pose && near(pose->vertices[1].normal[0], 3.0F) &&
            near(pose->vertices[2].normal[2], 4.25F),
            "shared source normal receives the same normal diff for all deduplicated vertices");

        auto halfway = built->evaluatePose(0, 1, 0.5F);
        expect(halfway && near(halfway->vertices[0].position[0], 12.5F) &&
            near(halfway->vertices[0].position[1], 23.0F) &&
            near(halfway->vertices[0].normal[0], 2.0F) &&
            near(halfway->vertices[0].normal[1], 2.0F),
            "MIMe interpolation uses poseA + (poseB-poseA)*amount for position and normal");
        expect(halfway && near(halfway->bounds.minimum[0], 12.5F) &&
            near(halfway->bounds.maximum[0], 27.5F) &&
            near(halfway->bounds.minimum[1], -63.0F) &&
            near(halfway->bounds.maximum[1], 23.0F),
            "MIMe bounds interpolate between pose bounding boxes like hmdload.cpp");

        auto extrapolated = built->evaluatePose(0, 1, 1.5F);
        expect(extrapolated && near(extrapolated->vertices[0].position[0], 17.5F) &&
            near(extrapolated->vertices[0].position[1], 29.0F),
            "MIMe interpolation amount remains intentionally unclamped");
        expect(!built->evaluatePose(-1, 0, 0.0F) &&
            !built->evaluatePose(0, 2, 0.0F),
            "invalid MIMe pose indices fail explicitly instead of inventing retained state");

        auto render = makeMeshRenderData(*built, 0, 1, 0.5F);
        expect(render && render->vertices.size() == 3 && render->indices.size() == 3 &&
            render->batches.size() == 1 &&
            near(render->vertices[0].position[0], 12.5F),
            "pose-aware renderer data preserves topology while replacing only evaluated vertices");
    }

    void testResourceScopedCache()
    {
        ResourceFixture fixture;
        expect(writeResources(fixture.root, flatTriangle()),
            "synthetic eight-bank resource set with HMD payload is written");
        ResourceRuntime runtime;
        const auto paths = ResourcePaths::create(std::array{fixture.root});
        expect(paths && runtime.initialize(*paths).has_value(),
            "resource snapshot for MESHX cache initializes");
        auto snapshot = runtime.snapshot();
        MeshRuntimeCache cache(snapshot);
        const auto id = packDataId(LegacyGroupId::ThreeD, 0);
        auto first = cache.resolve(id);
        expect(first && (*first)->mesh && (*first)->renderData &&
            (*first)->renderData->indices.size() == 3 && cache.size() == 1,
            "MESHX cache resolves HMD through the immutable resource snapshot once");
        auto second = cache.resolve(id);
        expect(second && first && first->get() == second->get() && cache.size() == 1,
            "repeated MESHX resolution reuses the exact immutable asset");
        runtime.shutdown();
        snapshot.reset();
        auto afterShutdown = cache.resolve(id);
        expect(afterShutdown && first && afterShutdown->get() == first->get() &&
            cache.resources(),
            "cache-owned resource snapshot and MESHX asset survive service shutdown");
        cache.clear();
        expect(cache.size() == 0 && first && (*first)->mesh->source()->bytes().size() == 112,
            "external asset handles survive cache eviction through immutable HMD ownership");
    }

    void testEmbeddedTextureResolution()
    {
        auto source = parse(texturedTriangleWithEmbeddedImage());
        auto built = MeshXRuntime::build(source);
        expect(built && built->groups().size() == 1 && built->groups()[0].texture &&
            built->groups()[0].texture->sourceImage,
            "embedded GsUIMG1 resolves textured MESHX without an external resolver");
        if (!built) return;
        const auto& texture = *built->groups()[0].texture;
        expect(texture.page == 128 && texture.x == 0 && texture.y == 0 &&
            texture.width == 4 && texture.height == 2 && texture.sourceImage->rgba.size() == 32,
            "automatic FindTexture region retains decoded HMD image ownership");
        const auto& vertices = built->vertices();
        expect(vertices.size() == 3 && near(vertices[0].uv[0], 0.0F) &&
            near(vertices[0].uv[1], 0.0F) && near(vertices[1].uv[0], 0.75F) &&
            near(vertices[2].uv[1], 0.5F),
            "embedded texture UVs use the historical local region coordinates");
        const auto render = makeMeshRenderData(*built);
        expect(render.batches.size() == 1 && render.batches[0].texture &&
            render.batches[0].texture->sourceImage.get() == texture.sourceImage.get(),
            "render batch keeps the immutable embedded texture pixels alive");

        auto malformed = texturedTriangleWithEmbeddedImage();
        setWord(malformed, 24, 0x7FFFFFFF);
        auto bad = MeshXRuntime::build(parse(std::move(malformed)));
        expect(!bad && bad.error().code == MeshRuntimeErrorCode::TextureDecodeFailed &&
            bad.error().sourceError &&
            bad.error().sourceError->code == MeshDataErrorCode::RangeOutOfBounds,
            "malformed embedded CLUT propagates a precise texture decode failure");
    }
    void testTextureResolutionAndHistoricalDrop()
    {
        auto source = parse(texturedTriangle());
        auto unresolved = MeshXRuntime::build(source);
        expect(!unresolved &&
            unresolved.error().code == MeshRuntimeErrorCode::NoRenderableGeometry &&
            unresolved.error().skippedTexturedTriangles == 1,
            "a textured triangle without a resolved historical texture is dropped");

        const MeshTextureResolver resolver = [](const MeshTextureLookup& lookup)
            -> std::optional<MeshTextureRegion>
        {
            if (lookup.page != 0x1234 || lookup.u != 0x11 || lookup.v != 0x22)
                return std::nullopt;
            return MeshTextureRegion{99, lookup.page, 0, 0, 256, 128};
        };
        auto built = MeshXRuntime::build(std::move(source), resolver);
        expect(built && built->groups().size() == 1 &&
            built->groups()[0].texture && built->groups()[0].texture->key == 99,
            "textured MESHX group retains the resolved texture identity and page");
        if (!built) return;
        const auto& vertices = built->vertices();
        expect(near(vertices[0].uv[0], 0x11 / 256.0F) &&
            near(vertices[0].uv[1], 0x22 / 128.0F) &&
            near(vertices[2].uv[0], 0x55 / 256.0F) &&
            near(vertices[2].uv[1], 0x66 / 128.0F),
            "texture coordinates use the source region origin and dimensions");
        const auto render = makeMeshRenderData(*built);
        expect(render.batches[0].texture &&
            render.batches[0].texture->page == 0x1234,
            "render data preserves texture references without owning a renderer resource");
    }
}

int main()
{
    testUntexturedMeshAndRenderData();
    testMimePoseEvaluation();
    testResourceScopedCache();
    testEmbeddedTextureResolution();
    testTextureResolutionAndHistoricalDrop();
    std::cout << (failures ? "MESHX runtime tests FAILED\n" :
        "MESHX runtime tests passed\n");
    return failures ? 1 : 0;
}
