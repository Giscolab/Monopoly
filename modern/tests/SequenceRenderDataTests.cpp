#include "SequenceRenderData.hpp"
#include "LegacyDataArchiveBuilder.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>

namespace
{
    using namespace monopoly::data;
    using namespace monopoly::sequence;
    int failures{};
    void expect(bool value, std::string_view text)
    { std::cout << (value ? "[PASS] " : "[FAIL] ") << text << '\n'; if (!value) ++failures; }
    void word(DataBytes& bytes, std::uint32_t value)
    { for (unsigned s = 0; s != 32; s += 8) bytes.push_back(static_cast<std::byte>((value >> s) & 255)); }
    DataBytes words(std::initializer_list<std::uint32_t> values)
    { DataBytes r; for (auto value : values) word(r, value); return r; }
    DataBytes chunk(std::uint8_t id, const DataBytes& payload)
    { DataBytes r; word(r, (static_cast<std::uint32_t>(id) << 24) | static_cast<std::uint32_t>(payload.size() + 4)); r.insert(r.end(), payload.begin(), payload.end()); return r; }
    DataBytes meshSequence(DataId mesh, const DataBytes& attributes = {})
    {
        DataBytes payload;
        word(payload, 0); word(payload, (4U << 24U) | 0x4000'0000U);
        word(payload, 2U | 16U); word(payload, mesh);
        payload.insert(payload.end(), attributes.begin(), attributes.end());
        return chunk(9, payload);
    }
    DataBytes meshChoice(std::int16_t a, std::int16_t b, std::uint32_t floatBits)
    {
        return chunk(139, words({
            static_cast<std::uint16_t>(a) |
                (static_cast<std::uint32_t>(static_cast<std::uint16_t>(b)) << 16U),
            floatBits}));
    }
    DataBytes flatHmd()
    {
        return words({0x01020304,0,6,2,11,0,1,3,0x80000011,0x80000014,0x8000001A,
            0xFFFFFFFF,7,0x80000001,0x00000008,0x80010002,0,0x00563412,0,0x00020001,
            0xFFEC000A,30,0x00280014,60,0x003C001E,90,0xF8001000,1024});
    }
    DataBytes animatedHmd()
    {
        DataBytes data(52U * 4U);
        auto set = [&](std::size_t index, std::uint32_t value)
        {
            for (unsigned shift = 0; shift != 32; shift += 8)
                data[index * 4U + shift / 8U] =
                    static_cast<std::byte>((value >> shift) & 255U);
        };
        set(0,0x01020304); set(1,0); set(2,5); set(3,1); set(4,11);
        set(5,1); set(6,4); set(7,0x80000017); set(8,0x8000001A);
        set(9,0x80000020); set(10,0x80000022);
        set(11,0xFFFFFFFF); set(12,6); set(13,0x80000003);
        set(14,0x00000008); set(15,0x80010002); set(16,0);
        set(17,0x04010020); set(18,0x80010002); set(19,0);
        set(20,0x04010021); set(21,0x80010002); set(22,3);
        set(23,0x00563412); set(24,0); set(25,0x00020001);
        set(26,0xFFEC000A); set(27,30); set(28,0x00280014); set(29,60);
        set(30,0x003C001E); set(31,90); set(32,0xF8001000); set(33,1024);
        set(34,0x00010000); set(35,0); set(36,6);
        set(37,0x00000001); set(38,0); set(39,14);
        set(40,0); set(41,0x00030000);
        set(42,0xFFFA0005); set(43,7); set(44,0); set(45,0);
        set(46,0x0006FFFB); set(47,0x0000FFF9);
        set(48,0); set(49,0x00010000); set(50,0xFFFD0002); set(51,4);
        return data;
    }

    struct Fixture
    {
        std::filesystem::path root;
        Fixture() : root(std::filesystem::current_path() / ("SequenceRenderData-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
        { std::filesystem::create_directories(root / "Dat_Mon"); }
        ~Fixture() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
    };
    bool writeResources(const std::filesystem::path& root,
        const DataBytes& sequenceBytes, const DataBytes& hmdBytes)
    {
        const std::array names{"dat_main.dat","dat_pat.dat","dat_bord.dat","dat_brd2.dat",
            "dat_3d.dat","dat_ln01.dat","dat_lm01.dat","dat_lk01.dat"};
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            std::vector<ArchiveBuildItem> items;
            if (i == 0) items.push_back({LegacyDataType::Chunky, sequenceBytes});
            else if (i == 4) items.push_back({LegacyDataType::Hmd, hmdBytes});
            else if (i == 5)
            {
                items.push_back({LegacyDataType::IndexTable,
                    {std::byte{42},std::byte{0},std::byte{0},std::byte{0},std::byte{1},std::byte{0}}});
                items.push_back({LegacyDataType::String,
                    {std::byte{'A'},std::byte{0},std::byte{0},std::byte{0}}});
            }
            else items.push_back({LegacyDataType::Native,{std::byte{1}}});
            if (!writeLegacyDataArchive(root / "Dat_Mon" / names[i], items)) return false;
        }
        return true;
    }

    void testAnimatedSequenceToRenderData()
    {
        Fixture fixture;
        const auto sequenceBytes = meshSequence(
            packDataId(LegacyGroupId::ThreeD, 0),
            meshChoice(0, 1, 0x3F000000U)); // 0.5f
        expect(writeResources(fixture.root, sequenceBytes, animatedHmd()),
            "animated sequence/HMD resource fixture is written");
        ResourceRuntime resources;
        auto paths = ResourcePaths::create(std::array{fixture.root});
        expect(paths && resources.initialize(*paths).has_value(),
            "animated resource snapshot initializes");
        auto snapshot = resources.snapshot();
        auto program = SequenceProgram::load(snapshot,
            packDataId(LegacyGroupId::Main, 0));
        expect(program.has_value(), "mesh-choice sequence program loads");
        if (!program) return;
        SequenceRuntime runtime;
        auto root = runtime.start(*program, 11);
        expect(root && runtime.update(0).has_value(),
            "mesh-choice sequence starts and evaluates its initial state");
        MeshRuntimeCache meshes(snapshot);
        auto render = collectSequenceMeshRenderData(runtime, meshes);
        expect(render && render->size() == 1 &&
            (*render)[0].meshChoice.meshIndexA == 0 &&
            (*render)[0].meshChoice.meshIndexB == 1 &&
            (*render)[0].meshChoice.meshProportion == 0.5F,
            "chunk 139 choice reaches sequence render publication unchanged");
        if (!render || render->empty()) return;
        const auto& item = (*render)[0];
        expect(item.renderData && item.renderData != item.asset->renderData &&
            item.renderData->vertices.size() == 3 &&
            item.renderData->indices == item.asset->renderData->indices,
            "animated publication replaces vertices while preserving cached topology");
        expect(item.renderData &&
            std::fabs(item.renderData->vertices[0].position[0] - 12.5F) < 0.0001F &&
            std::fabs(item.renderData->vertices[0].position[1] - 23.0F) < 0.0001F &&
            std::fabs(item.renderData->bounds.minimum[0] - 12.5F) < 0.0001F,
            "sequence mesh choice evaluates the HMD MIMe pose and animated bounds");
    }

    void testSequenceToRenderData()
    {
        Fixture fixture;
        expect(writeResources(fixture.root,
            meshSequence(packDataId(LegacyGroupId::ThreeD, 0)), flatHmd()),
            "sequence/HMD resource fixture is written");
        ResourceRuntime resources;
        auto paths = ResourcePaths::create(std::array{fixture.root});
        expect(paths && resources.initialize(*paths).has_value(), "resource snapshot initializes");
        auto snapshot = resources.snapshot();
        auto program = SequenceProgram::load(snapshot, packDataId(LegacyGroupId::Main, 0));
        expect(program.has_value(), "3D mesh sequence program loads from immutable snapshot");
        if (!program) return;
        SequenceRuntime runtime;
        auto root = runtime.start(*program, 7);
        expect(root && runtime.update(0).has_value(), "3D mesh runtime starts and updates");
        MeshRuntimeCache meshes(snapshot);
        auto render = collectSequenceMeshRenderData(runtime, meshes);
        expect(render && render->size() == 1 && (*render)[0].node == *root &&
            (*render)[0].contentsDataId == packDataId(LegacyGroupId::ThreeD, 0) &&
            (*render)[0].asset->renderData->indices.size() == 3,
            "active sequence resolves transactionally to immutable CPU render data");
        const auto firstAsset = (*render)[0].asset;
        const auto moved = runtime.moveMatching(packDataId(LegacyGroupId::Main, 0), 7,
            SequenceTransform(translate3D(3.0F, 4.0F, 5.0F)));
        expect(moved == 1 && runtime.update(0).has_value(),
            "runtime move is reevaluated before render-data publication");
        render = collectSequenceMeshRenderData(runtime, meshes);
        expect(render && (*render)[0].asset == firstAsset && meshes.size() == 1 &&
            (*render)[0].worldTransform.values[12] == 3.0F &&
            (*render)[0].worldTransform.values[13] == 4.0F &&
            (*render)[0].worldTransform.values[14] == 5.0F,
            "render publication reuses mesh asset while carrying current world transform");
        resources.shutdown();
        snapshot.reset();
        render = collectSequenceMeshRenderData(runtime, meshes);
        expect(render && (*render)[0].asset == firstAsset,
            "published sequence render data survives ResourceRuntime shutdown via snapshot ownership");
        runtime.stopAll();
        render = collectSequenceMeshRenderData(runtime, meshes);
        expect(render && render->empty(), "stopped sequence publishes no stale render item");
    }
}

int main()
{
    testAnimatedSequenceToRenderData();
    testSequenceToRenderData();
    std::cout << "Sequence render-data failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
