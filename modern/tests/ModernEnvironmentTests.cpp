#include "ModernEnvironment.hpp"
#include "SequenceTransforms.hpp"

#include <atomic>
#include <thread>
#include <bit>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace monopoly;
    int failures{};
    void expect(bool condition, std::string_view message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) ++failures;
    }
    bool close(float actual, float expected) { return std::abs(actual - expected) < 0.001F; }
    std::array<float, 3> point(const std::array<float, 3>& value, const sequence::Matrix3D& matrix)
    {
        const auto& m = matrix.values;
        return {value[0] * m[0] + value[1] * m[4] + value[2] * m[8] + m[12],
            value[0] * m[1] + value[1] * m[5] + value[2] * m[9] + m[13],
            value[0] * m[2] + value[1] * m[6] + value[2] * m[10] + m[14]};
    }
    void word(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
    struct Fixture
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("monopoly-environment-fixture-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        Fixture()
        {
            std::filesystem::create_directories(root / "environment");
            std::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2}]}],"buffers":[{"byteLength":84}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":12}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0.25,0],"max":[1,1.25,1]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5125,"count":3,"type":"SCALAR"}]})";
            while (json.size() % 4U) json.push_back(' ');
            std::vector<std::uint8_t> binary;
            // Asymmetric geometry with authored bottom Y=.25 proves rotation
            // handedness, scale and preservation of the original ground offset.
            for (const float value : {0.0F, 0.25F, 0.0F, 1.0F, 0.25F, 0.0F,
                    0.0F, 1.25F, 1.0F, 0.0F, 1.0F, 0.0F,
                    0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F})
                word(binary, std::bit_cast<std::uint32_t>(value));
            for (const auto index : {0U, 1U, 2U}) word(binary, index);
            std::vector<std::uint8_t> bytes;
            word(bytes, 0x46546C67U); word(bytes, 2U);
            word(bytes, static_cast<std::uint32_t>(28U + json.size() + binary.size()));
            word(bytes, static_cast<std::uint32_t>(json.size())); word(bytes, 0x4E4F534AU);
            bytes.insert(bytes.end(), json.begin(), json.end());
            word(bytes, static_cast<std::uint32_t>(binary.size())); word(bytes, 0x004E4942U);
            bytes.insert(bytes.end(), binary.begin(), binary.end());
            for (const auto& definition : engine::modernEnvironmentDefinitions())
            {
                std::ofstream output(root / definition.relativeGlbPath, std::ios::binary);
                output.write(reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
                if (!output) throw std::runtime_error("cannot write environment fixture");
            }
        }
        ~Fixture()
        {
            std::error_code ignored;
            for (const auto& definition : engine::modernEnvironmentDefinitions())
                std::filesystem::remove(root / definition.relativeGlbPath, ignored);
            std::filesystem::remove(root / "environment", ignored);
            std::filesystem::remove(root, ignored);
        }
    };
}

int main()
{
    using namespace monopoly;
    Fixture fixture;
    std::vector<std::string> reports;
    auto diagnostic = [&](std::string_view message) { reports.emplace_back(message); };
    const auto identity = sequence::identity3D();
    engine::ModernEnvironment environment(fixture.root, false, diagnostic);
    expect(environment.items(identity, 0).empty() && reports.empty(),
        "environment is off by default and produces no diagnostics or scene instances");
    expect(environment.decodeWorkers()>=1 && environment.decodeWorkers()<=8,
        "production worker default remains bounded on every hardware count");
    environment.setEnabled(true);
    const auto original = environment.items(identity, 77);
    expect(original.size() == 3U && reports.empty(), "production loader creates three optional native decorations");
    if (original.size() != 3U) return 1;
    constexpr std::array<std::array<float, 3>, 3> Centers{{
        {2830.0F, 24.0F, 2430.0F}, {3610.0F, 24.0F, 3530.0F}, {3250.0F, 0.0F, 1650.0F}}};
    for (std::size_t index = 0; index < original.size(); ++index)
    {
        const auto& item = original[index];
        const auto center = point({0.0F, 0.0F, 0.0F}, item.worldTransform);
        expect(close(center[0], Centers[index][0]) && close(center[1], Centers[index][1]) &&
            close(center[2], Centers[index][2]), "authored center maps into aligned board source frame");
        expect(data::dataGroup(item.contentsDataId) == 0xFFFBU &&
            data::dataTag(item.contentsDataId) == index && item.rootSequenceDataId == data::EmptyDataId &&
            (item.node & (std::uint64_t{1} << 63U)) != 0U &&
            item.asset->origin == data::MeshAssetOrigin::ModernGltf && !item.asset->mesh,
            "native logical IDs and high-bit nodes never impersonate a retail HMD or sequence");
        expect(item.clock == 77 && close(item.renderData->bounds.minimum[1], 50.0F) &&
            close(item.renderData->vertices[1].position[2], 200.0F) &&
            close(item.renderData->vertices[2].position[0], -200.0F),
            "production geometry keeps authored ground and uses proper minus90 rotation with scale200");
    }
    auto board = identity;
    board.values[0] = board.values[5] = board.values[10] = 0.10F;
    const auto scaled = environment.items(board, 78);
    const auto scaledCenter = point({0.0F, 0.0F, 0.0F}, scaled[0].worldTransform);
    expect(close(scaledCenter[0], 283.0F) && close(scaledCenter[1], 2.4F) &&
        close(scaledCenter[2], 243.0F), "actual board point10 scale also scales source placement2430 into world243");
    auto moved = identity;
    // Independent proper quarter-turn and .10 scale, followed by translation.
    moved.values = {0.0F, 0.0F, -0.10F, 0.0F, 0.0F, 0.10F, 0.0F, 0.0F,
        0.10F, 0.0F, 0.0F, 0.0F, 10.0F, 20.0F, 30.0F, 1.0F};
    const auto transformed = environment.items(moved, 79);
    const auto vertex = point(transformed[0].renderData->vertices[0].position, transformed[0].worldTransform);
    expect(close(vertex[0], 253.0F) && close(vertex[1], 27.4F) && close(vertex[2], -253.0F),
        "geometry and placement follow real board rotation scale and translation in row-vector order");
    expect(original[0].asset == transformed[0].asset && original[0].renderData == transformed[0].renderData,
        "board motion reuses the same immutable native mesh owner");
    environment.setEnabled(false);
    expect(environment.items(board, 80).empty(), "disabling environment retires all scene instances");
    environment.setEnabled(true);
    expect(environment.items(board, 81)[0].asset == original[0].asset,
        "reenabling reuses already decoded immutable geometry");

    reports.clear();
    engine::ModernEnvironment absent(fixture.root / "absent", true, diagnostic);
    expect(absent.items(identity, 0).empty() && absent.items(identity, 1).empty() && reports.size() == 3U,
        "missing optional files produce no items and only one diagnostic per asset");
    using LoadResult = std::expected<std::shared_ptr<const data::MeshRenderData>, data::MeshRuntimeError>;
    reports.clear();
    std::size_t attempts{};
    auto partialLoader = [&](const std::filesystem::path& path, data::ModernGltfLoadOptions options) -> LoadResult
    {
        ++attempts;
        if (path.filename() == "paris_fountain.glb")
            return std::unexpected(data::MeshRuntimeError{data::MeshRuntimeErrorCode::ModernAssetInvalid,
                "rejected fixture"});
        return data::loadModernGltfMesh(path, options);
    };
    engine::ModernEnvironment partial(fixture.root, true, diagnostic, partialLoader);
    const auto surviving = partial.items(identity, 0);
    expect(surviving.size() == 2U && partial.items(identity, 1).size() == 2U && attempts == 3U && reports.size() == 1U,
        "one rejected decoration skips once while other decorations continue");
    auto invalid = std::make_shared<data::MeshRenderData>(*original[0].renderData);
    invalid->indices[0] = 999U;
    reports.clear();
    engine::ModernEnvironment rejected(fixture.root, true, diagnostic,
        [invalid](const std::filesystem::path&, data::ModernGltfLoadOptions) -> LoadResult { return invalid; });
    expect(rejected.items(identity, 0).empty() && rejected.items(identity, 1).empty() && reports.size() == 3U,
        "invalid native geometry never enters sequence render items");
    reports.clear();
    const auto rejectedPointer = original[0].renderData.get();
    environment.rejectGeometry(rejectedPointer);
    environment.rejectGeometry(rejectedPointer);
    environment.rejectGeometry(nullptr);
    expect(environment.items(identity, 90).size() == 2U && reports.size() == 1U,
        "GPU rejection drops native geometry permanently and diagnoses only once");
    environment.setEnabled(false);
    environment.setEnabled(true);
    expect(environment.items(identity, 91).size() == 2U,
        "rejected decoration cannot reload after reenabling");
    reports.clear();
    auto badMatrix = identity;
    badMatrix.values[0] = std::numeric_limits<float>::quiet_NaN();
    expect(environment.items(badMatrix, 0).empty() && environment.items(badMatrix, 1).empty() && reports.size() == 1U,
        "invalid board transform drops decoration instances with a single diagnostic");
    std::size_t presentationAttempts{};
    engine::ModernEnvironment dressing(fixture.root, true, diagnostic,
        [&](const std::filesystem::path& path, data::ModernGltfLoadOptions options) -> LoadResult
        {
            ++presentationAttempts;
            if(path.parent_path().filename()=="presentation")
                return std::make_shared<const data::MeshRenderData>(*original[0].renderData);
            return data::loadModernGltfMesh(path,options);
        });
    expect(dressing.items(board,0).size()==3 && presentationAttempts==3,
        "default environment does not attempt presentation assets");
    const auto dressed=dressing.items(board,1,true);
    expect(dressed.size()==5 && presentationAttempts==5 &&
        dressed[3].node!=dressed[4].node && dressed[3].node!=dressed[0].node,
        "opt-in dressing uses distinct native identities and loads each geometry once");
    const auto supportOrigin=point({0,0,0},dressed[4].worldTransform);
    expect(close(supportOrigin[0],243)&&close(supportOrigin[1],0)&&close(supportOrigin[2],243),
        "presentation support inherits the actual board origin and scale");
    const auto tabletopOnly=dressing.items(board,2,true,false);
    expect(tabletopOnly.size()==2 && tabletopOnly[0].node==dressed[3].node &&
        tabletopOnly[1].node==dressed[4].node && presentationAttempts==5,
        "USA tabletop does not introduce Paris landmarks or reload geometry");
    dressing.rejectGeometry(dressed[4].renderData.get());
    expect(dressing.items(board,2,true).size()==4 && dressing.items(board,3).size()==3 && presentationAttempts==5,
        "failed support drops independently without retiring existing landmarks or retrying");
    std::size_t cityAttempts{};
    engine::ModernEnvironment city(fixture.root,true,diagnostic,
        [&](const std::filesystem::path&,data::ModernGltfLoadOptions options)->LoadResult
        {
            ++cityAttempts;
            expect(options.unitsPerMeter==200 && options.yawDegrees==0 &&
                !options.groundToZero,"global procedural city preserves authored Y and proper orientation");
            return std::make_shared<const data::MeshRenderData>(*original[0].renderData);
        });
    const auto completeCity=city.items(board,0,true,true,true);
    expect(completeCity.size()==engine::ModernCityCount+2 && cityAttempts==engine::ModernCityCount+2,
        "complete procedural city includes independent groups and support without duplicate local landmarks");
    std::vector<sequence::SequenceNodeId> cityNodes;
    for(const auto& item:completeCity)
    {
        const auto origin=point({0,0,0},item.worldTransform);
        expect(close(origin[0],243) && close(origin[1],0) && close(origin[2],243) &&
            item.rootSequenceDataId==data::EmptyDataId && item.asset->origin==data::MeshAssetOrigin::ModernGltf,
            "native city groups inherit actual board origin without a fabricated retail sequence");
        cityNodes.push_back(item.node);
    }
    std::sort(cityNodes.begin(),cityNodes.end());
    expect(std::adjacent_find(cityNodes.begin(),cityNodes.end())==cityNodes.end(),
        "all procedural city and support identities remain distinct");
    city.rejectGeometry(completeCity.back().renderData.get());
    expect(city.items(board,1,true,true,true).size()==completeCity.size()-1 &&
        cityAttempts==completeCity.size(),"rejected city group falls back independently and is never retried");
    // Injected bad_alloc qualifies exception containment, not actual heap exhaustion.
    // Explicitly concurrent injected loader uses only atomic accounting and
    // immutable shared geometry. Default injected callbacks above stay serial.
    for(const unsigned workers:{1U,4U,8U})
    {
        std::atomic<unsigned> active{},peak{},calls{};
        const auto caller=std::this_thread::get_id();
        bool diagnosticsOnCaller=true;
        std::vector<std::string> orderedErrors;
        engine::ModernEnvironment parallel(fixture.root,true,
            [&](std::string_view error)
            {diagnosticsOnCaller&=std::this_thread::get_id()==caller;orderedErrors.emplace_back(error);},
            [&](const std::filesystem::path& path,data::ModernGltfLoadOptions)->LoadResult
            {
                ++calls;const auto count=++active;auto old=peak.load();
                while(old<count&&!peak.compare_exchange_weak(old,count)){}
                std::this_thread::sleep_for(std::chrono::milliseconds(2));--active;
                const auto name=path.filename().string();
                if(name=="procedural_city_buildings_back.glb")throw std::runtime_error("qualified thrown failure");
                if(name=="procedural_city_trees_b.glb")throw std::bad_alloc{};
                if(name=="procedural_city_trees_a.glb")return std::unexpected(data::MeshRuntimeError{data::MeshRuntimeErrorCode::ModernAssetInvalid,"qualified returned failure"});
                return original[0].renderData;
            });
        parallel.setDecodeWorkers(workers);
        const auto batch=parallel.items(board,0,true,false,true);
        expect(batch.size()==engine::ModernCityCount-1 && calls==engine::ModernCityCount+2 &&
            peak<=workers && diagnosticsOnCaller && orderedErrors.size()==3,
            "bounded workers contain injected bad_alloc/runtime/returned failures and publish diagnostics only on caller");
        expect(orderedErrors.size()==3 && orderedErrors[0].find("city back")!=std::string::npos &&
            orderedErrors[1].find("city trees a")!=std::string::npos &&
            orderedErrors[2].find("city trees b")!=std::string::npos,
            "parallel failures retain catalog diagnostic order regardless of completion order");
        bool order=true;
        for(std::size_t i=1;i<batch.size();++i)order&=batch[i-1].node<batch[i].node;
        expect(order && std::all_of(batch.begin(),batch.end(),[&](const auto& item)
            {return item.renderData==original[0].renderData && item.asset->origin==data::MeshAssetOrigin::ModernGltf;}),
            "1/4/8 workers preserve ordered logical identities and immutable geometry/material owner");
        const auto next=parallel.items(board,1,true,false,true);
        expect(next.size()==batch.size() && calls==engine::ModernCityCount+2,
            "parallel success and failure are attempted once, repeated publication is cached");
        parallel.setDecodeWorkers(100);
        expect(parallel.decodeWorkers()==8,"explicit worker count is bounded at8");
    }
    return failures ? 1 : 0;

}
