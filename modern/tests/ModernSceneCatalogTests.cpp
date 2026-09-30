#include "ModernSceneCatalog.hpp"
#include "Display.hpp"
#include "PieceBuildingDisplay.hpp"
#include "RuleTypes.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <string_view>

namespace
{
    int failures{};
    void expect(bool condition, std::string_view message)
    {
        if (condition) std::cout << "[PASS] " << message << '\n';
        else { std::cerr << "[FAIL] " << message << '\n'; ++failures; }
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
    return failures ? 1 : 0;
}
