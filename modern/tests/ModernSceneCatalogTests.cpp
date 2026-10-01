#include "ModernSceneCatalog.hpp"
#include "Display.hpp"
#include "PieceBuildingDisplay.hpp"
#include "RuleTypes.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
    int failures{};
    void expect(bool condition, std::string_view message)
    {
        if (condition) std::cout << "[PASS] " << message << '\n';
        else { std::cerr << "[FAIL] " << message << '\n'; ++failures; }
    }

    std::filesystem::path hotelFixture()
    {
        std::vector<std::uint8_t> binary;
        const auto word = [&](std::uint32_t value) {
            for (unsigned shift=0;shift<32;shift+=8)
                binary.push_back(static_cast<std::uint8_t>(value>>shift)); };
        for (const float value : std::array<float,9>{-.325F,0,-.45F,.325F,0,-.45F,0,.775F,.45F})
            word(std::bit_cast<std::uint32_t>(value));
        for (unsigned vertex=0;vertex<3;++vertex)
            for (const float value : std::array<float,3>{0,0,-1}) word(std::bit_cast<std::uint32_t>(value));
        word(0);word(1);word(2);
        std::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2}]}],"buffers":[{"byteLength":84}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":12}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-0.325,0,-0.45],"max":[0.325,0.775,0.45]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5125,"count":3,"type":"SCALAR"}]})";
        while (json.size()%4) json.push_back(' ');
        std::vector<std::uint8_t> output;
        const auto outputWord = [&](std::uint32_t value) {
            for (unsigned shift=0;shift<32;shift+=8)
                output.push_back(static_cast<std::uint8_t>(value>>shift)); };
        outputWord(0x46546C67U);outputWord(2);
        outputWord(static_cast<std::uint32_t>(28+json.size()+binary.size()));
        outputWord(static_cast<std::uint32_t>(json.size()));outputWord(0x4E4F534AU);
        output.insert(output.end(),json.begin(),json.end());
        outputWord(static_cast<std::uint32_t>(binary.size()));outputWord(0x004E4942U);
        output.insert(output.end(),binary.begin(),binary.end());
        const auto path=std::filesystem::temp_directory_path()/
            ("monopoly-hotel-contract-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".glb");
        std::ofstream file(path,std::ios::binary);
        file.write(reinterpret_cast<const char*>(output.data()),static_cast<std::streamsize>(output.size()));
        return path;
    }
}

int main()
{
    using namespace monopoly;
    using namespace data;
    const std::array<std::string_view, 0> noArguments{};
    const auto defaultArguments = parseModernSceneArguments(noArguments);
    expect(defaultArguments && !defaultArguments->options.parisBoard &&
        !defaultArguments->options.house && !defaultArguments->options.environment &&
        defaultArguments->remaining.empty(),
        "absent scene flags keep retail defaults");
    const std::array<std::string_view, 6> configuredArguments{
        "--language=fr", "--modern-board=paris", "--modern-buildings=house", "--modern-environment=paris", "--smoke", "asset-root"};
    const auto parsed = parseModernSceneArguments(configuredArguments);
    expect(parsed && parsed->options.parisBoard && parsed->options.house && parsed->options.environment &&
        parsed->remaining == std::vector<std::string_view>{"--language=fr", "--smoke", "asset-root"},
        "scene parser extracts explicit options and preserves unrelated arguments in order");
    const std::array<std::string_view, 3> retailArguments{
        "--modern-board=retail", "--modern-buildings=retail", "--modern-environment=retail"};
    const auto retail = parseModernSceneArguments(retailArguments);
    expect(retail && !retail->options.parisBoard && !retail->options.house && !retail->options.environment,
        "explicit retail values disable optional board and building replacements");
    for (const auto invalid : {"--modern-board", "--modern-board=", "--modern-board=other",
            "--modern-buildings", "--modern-buildings=", "--modern-buildings=hotel",
            "--modern-environment", "--modern-environment=", "--modern-environment=other"})
    {
        const std::array<std::string_view, 1> bad{invalid};
        expect(!parseModernSceneArguments(bad), "missing or unknown scene option value is rejected");
    }
    const std::array<std::string_view, 2> duplicateBoard{
        "--modern-board=paris", "--modern-board=retail"};
    const std::array<std::string_view, 2> duplicateBuildings{
        "--modern-buildings=house", "--modern-buildings=house"};
    const std::array<std::string_view, 2> duplicateEnvironment{
        "--modern-environment=paris", "--modern-environment=retail"};
    expect(!parseModernSceneArguments(duplicateBoard) && !parseModernSceneArguments(duplicateBuildings) &&
        !parseModernSceneArguments(duplicateEnvironment),
        "duplicate scene options are rejected even when repeated values agree");
    ModernSceneOptions options;
    ModernSceneContext context{BoardEdition::Europe, 1, LanguageId::French, 12, {}};
    const auto board = modernSceneDefinition(ModernSceneKind::ParisBoard).legacyMeshId;
    const auto house = modernSceneDefinition(ModernSceneKind::House).legacyMeshId;
    const auto hotel = modernSceneDefinition(ModernSceneKind::Hotel).legacyMeshId;
    expect(board == 0x00080003U && house == 0x00080005U,
        "catalog identities match classic medium board and house retail IDs");
    const auto boardQualified = [&](const ModernSceneContext& candidate)
    {
        return qualifiedModernSceneSequence(ModernSceneKind::ParisBoard,
            board, board, display::Board3DPriority, options, candidate);
    };
    expect(!boardQualified(context) && !qualifiedModernSceneSequence(ModernSceneKind::House,
        house, house, pieces::BoardHousingPriority, options, context),
        "ordinary retail options disable both scene replacements");
    options.parisBoard = true;
    expect(boardQualified(context), "explicit Paris option accepts matching European French locale");
    auto changed = context;
    changed.edition = BoardEdition::Usa;
    expect(!boardQualified(changed), "USA board retains retail even with matching Paris locale IDs");
    changed = context; changed.city = 0;
    expect(!boardQualified(changed), "other European city retains retail");
    changed = context; changed.language = LanguageId::EnglishUk;
    expect(!boardQualified(changed), "non-French language retains retail");
    changed = context; changed.currency = 0;
    expect(!boardQualified(changed), "non-Euro currency retains retail");
    changed = context; changed.currency = 1;
    expect(!boardQualified(changed), "French francs cannot display the authored Euro prices");
    changed = context; changed.customBoardPath = "custom";
    expect(!boardQualified(changed), "custom board paths retain retail");
    for (const auto id : {board - 1U, board + 1U,
            packDataId(LegacyGroupId::Board, dataTag(board))})
        expect(!qualifiedModernSceneSequence(ModernSceneKind::ParisBoard, id, board,
            display::Board3DPriority, options, context), "neighbor/foreign mesh cannot become Paris board");
    expect(!qualifiedModernSceneSequence(ModernSceneKind::ParisBoard, board, std::nullopt,
        display::Board3DPriority, options, context) &&
        !qualifiedModernSceneSequence(ModernSceneKind::ParisBoard, board, board + 1U,
            display::Board3DPriority, options, context), "missing or different board root retains retail");
    expect(!qualifiedModernSceneSequence(ModernSceneKind::ParisBoard, board, board,
        display::Board3DPriority - 1U, options, context) &&
        !qualifiedModernSceneSequence(ModernSceneKind::ParisBoard, board, board,
            display::Board3DPriority + 1U, options, context), "board requires exact production priority");
    const auto boardCalibration = modernSceneLoadOptions(ModernSceneKind::ParisBoard);
    expect(boardCalibration && boardCalibration->unitsPerMeter == 200.0F &&
        boardCalibration->localOffset == std::array<float, 3>{2430.0F, 0.0F, 2430.0F} &&
        boardCalibration->yawDegrees == 0.0F && !boardCalibration->groundToZero,
        "aligned board uses measured retail cell calibration without moving authored ground");
    const auto unknown = static_cast<ModernSceneKind>(255);
    expect(modernSceneDefinition(unknown).legacyMeshId == EmptyDataId &&
        !qualifiedModernSceneSequence(unknown, board, board,
            display::Board3DPriority, options, context) && !modernSceneLoadOptions(unknown),
        "unknown scene kinds have no replacement identity or calibration");
    options.house = true;
    const auto end = pieces::BoardHousingPriority + rules::SquareCount * pieces::HouseSlotCount;
    expect(qualifiedModernSceneSequence(ModernSceneKind::House, house, house,
        pieces::BoardHousingPriority, options, context) &&
        qualifiedModernSceneSequence(ModernSceneKind::House, house, house,
        pieces::BoardHousingPriority + 4U, options, context) &&
        qualifiedModernSceneSequence(ModernSceneKind::House, house, house,
            static_cast<std::uint16_t>(end - 1U), options, context),
        "houses on property squares qualify throughout production housing priorities");
    expect(!qualifiedModernSceneSequence(ModernSceneKind::House, house, house,
        pieces::BoardHousingPriority - 1U, options, context) &&
        !qualifiedModernSceneSequence(ModernSceneKind::House, house, house,
            static_cast<std::uint16_t>(end), options, context), "outside housing interval retains retail");
    expect(!qualifiedModernSceneSequence(ModernSceneKind::House, house - 1U, house,
        pieces::BoardHousingPriority, options, context) &&
        !qualifiedModernSceneSequence(ModernSceneKind::House, house, board,
            pieces::BoardHousingPriority, options, context) &&
        !qualifiedModernSceneSequence(ModernSceneKind::House, house, std::nullopt,
            pieces::BoardHousingPriority, options, context),
        "hotels and unrelated or missing roots cannot become houses");
    const auto calibration = modernSceneLoadOptions(ModernSceneKind::House);
    expect(calibration && std::abs(calibration->unitsPerMeter * 0.445803434 - 95.0) < 0.0001 &&
        calibration->yawDegrees == 0.0F && calibration->groundToZero &&
        calibration->localOffset == std::array<float, 3>{},
        "house calibration reproduces retail height and grounded centered pivot");
    expect(ModernSceneKindCount==3 && hotel==0x00080004U &&
        modernSceneDefinition(ModernSceneKind::Hotel).relativeGlbPath=="buildings/hotel.glb",
        "hotel has its own retail identity and optional asset path");
    expect(parsed && qualifiedModernSceneSequence(ModernSceneKind::Hotel,hotel,hotel,
        pieces::BoardHousingPriority,parsed->options,context),
        "existing buildings house option also enables separate hotel prototype");
    expect(!qualifiedModernSceneSequence(ModernSceneKind::Hotel,hotel,hotel,
        pieces::BoardHousingPriority,ModernSceneOptions{},context),
        "default retail options disable hotels");
    for (std::uint16_t priority=pieces::BoardHousingPriority;priority<end;++priority)
        expect(qualifiedModernSceneSequence(ModernSceneKind::Hotel,hotel,hotel,priority,options,context)==
            ((priority-pieces::BoardHousingPriority)%pieces::HouseSlotCount==0),
            "hotel eligibility matches slot-zero production housing priority");
    expect(!qualifiedModernSceneSequence(ModernSceneKind::Hotel,hotel,hotel,
        pieces::BoardHousingPriority-1U,options,context) &&
        !qualifiedModernSceneSequence(ModernSceneKind::Hotel,hotel,hotel,
            static_cast<std::uint16_t>(end),options,context),
        "hotel excludes priorities outside production housing interval");
    expect(!qualifiedModernSceneSequence(ModernSceneKind::Hotel,house,hotel,
        pieces::BoardHousingPriority,options,context) &&
        !qualifiedModernSceneSequence(ModernSceneKind::Hotel,hotel,house,
            pieces::BoardHousingPriority,options,context) &&
        !qualifiedModernSceneSequence(ModernSceneKind::Hotel,hotel,std::nullopt,
            pieces::BoardHousingPriority,options,context),
        "hotel requires exact mesh and HMD root without borrowing house identity");
    const auto hotelCalibration=modernSceneLoadOptions(ModernSceneKind::Hotel);
    expect(hotelCalibration && hotelCalibration->unitsPerMeter==200 && hotelCalibration->groundToZero &&
        hotelCalibration->yawDegrees==0 && hotelCalibration->localOffset==std::array<float,3>{},
        "hotel authoring contract uses explicit metre scale and retail grounded pivot");
    const auto fixturePath=hotelFixture();
    const auto hotelMesh=loadModernGltfMesh(fixturePath,*hotelCalibration);
    expect(hotelMesh && qualifiedModernSceneGeometry(ModernSceneKind::Hotel,**hotelMesh),
        "production GLB loader reproduces measured retail hotel bounds from authoring contract");
    if (hotelMesh)
    {
        auto wrong=**hotelMesh;
        wrong.bounds.maximum[1]+=1;
        expect(!qualifiedModernSceneGeometry(ModernSceneKind::Hotel,wrong),
            "incorrect hotel height cannot silently replace retail geometry");
        wrong=**hotelMesh;wrong.bounds.minimum[0]+=.002F;
        expect(!qualifiedModernSceneGeometry(ModernSceneKind::Hotel,wrong),
            "off-center hotel footprint outside .001 raw units is rejected");
        wrong=**hotelMesh;wrong.bounds.maximum[2]=std::numeric_limits<float>::quiet_NaN();
        expect(!qualifiedModernSceneGeometry(ModernSceneKind::Hotel,wrong),
            "nonfinite hotel bounds are rejected");
        expect(qualifiedModernSceneGeometry(ModernSceneKind::House,wrong),
            "hotel qualification does not change established house calibration");
    }
    std::filesystem::remove(fixturePath);
    expect(!loadModernGltfMesh(fixturePath,*hotelCalibration),
        "missing optional hotel asset yields loader failure for retail fallback");
    expect(!qualifiedModernSceneGeometry(ModernSceneKind::Hotel,MeshRenderData{}),
        "empty hotel geometry does not qualify");
    return failures ? 1 : 0;
}
