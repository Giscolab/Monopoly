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
    DataBytes meshSequence(DataId mesh)
    {
        DataBytes payload;
        word(payload, 0); word(payload, (4U << 24U) | 0x4000'0000U);
        word(payload, 2U | 16U); word(payload, mesh);
        return chunk(9, payload);
    }
    DataBytes flatHmd()
    {
        return words({0x01020304,0,6,2,11,0,1,3,0x80000011,0x80000014,0x8000001A,
            0xFFFFFFFF,7,0x80000001,0x00000008,0x80010002,0,0x00563412,0,0x00020001,
            0xFFEC000A,30,0x00280014,60,0x003C001E,90,0xF8001000,1024});
    }
    struct Fixture
    {
        std::filesystem::path root;
        Fixture() : root(std::filesystem::current_path() / ("SequenceRenderData-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
        { std::filesystem::create_directories(root / "Dat_Mon"); }
        ~Fixture() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
    };
    bool writeResources(const std::filesystem::path& root)
    {
        const std::array names{"dat_main.dat","dat_pat.dat","dat_bord.dat","dat_brd2.dat",
            "dat_3d.dat","dat_ln01.dat","dat_lm01.dat","dat_lk01.dat"};
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            std::vector<ArchiveBuildItem> items;
            if (i == 0) items.push_back({LegacyDataType::Chunky,
                meshSequence(packDataId(LegacyGroupId::ThreeD, 0))});
            else if (i == 4) items.push_back({LegacyDataType::Hmd, flatHmd()});
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

    void testSequenceToRenderData()
    {
        Fixture fixture;
        expect(writeResources(fixture.root), "sequence/HMD resource fixture is written");
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
    testSequenceToRenderData();
    std::cout << "Sequence render-data failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
