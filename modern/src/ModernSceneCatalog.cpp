#include "ModernSceneCatalog.hpp"

#include "Display.hpp"
#include "PieceBuildingDisplay.hpp"
#include "RuleTypes.hpp"
#include "TextureCatalog.hpp"

namespace monopoly::data
{
    namespace
    {
        constexpr ModernSceneDefinition ParisBoard{
            ModernSceneKind::ParisBoard,
            boardMeshDataId(BoardMeshKind::ClassicMedium),
            "board/paris_board_runtime.glb"};
        constexpr ModernSceneDefinition House{
            ModernSceneKind::House,
            packDataId(LegacyGroupId::ThreeD, pieces::HouseMeshTag),
            "buildings/house.glb"};
        constexpr ModernSceneDefinition Unknown{};
        constexpr int ParisCity = 1;
        // The recovered printed prices use euros, not the retail francs mode.
        constexpr int EuroCurrency = 12;
        constexpr float RetailHouseHeight = 95.0F;
        constexpr double AuthoredHouseHeightMetres = 0.445803434;
    }

    std::expected<ParsedModernSceneOptions, std::string> parseModernSceneArguments(
        std::span<const std::string_view> arguments)
    {
        ParsedModernSceneOptions parsed;
        bool boardSeen{};
        bool buildingsSeen{};
        bool environmentSeen{};
        for (const auto argument : arguments)
        {
            if (argument == "--modern-board" || argument.starts_with("--modern-board="))
            {
                if (boardSeen) return std::unexpected("duplicate --modern-board option");
                boardSeen = true;
                if (argument == "--modern-board=paris") parsed.options.parisBoard = true;
                else if (argument == "--modern-board=retail") parsed.options.parisBoard = false;
                else return std::unexpected("--modern-board requires paris or retail");
            }
            else if (argument == "--modern-buildings" || argument.starts_with("--modern-buildings="))
            {
                if (buildingsSeen) return std::unexpected("duplicate --modern-buildings option");
                buildingsSeen = true;
                if (argument == "--modern-buildings=house") parsed.options.house = true;
                else if (argument == "--modern-buildings=retail") parsed.options.house = false;
                else return std::unexpected("--modern-buildings requires house or retail");
            }
            else if (argument == "--modern-environment" || argument.starts_with("--modern-environment="))
            {
                if (environmentSeen) return std::unexpected("duplicate --modern-environment option");
                environmentSeen = true;
                if (argument == "--modern-environment=paris") parsed.options.environment = true;
                else if (argument == "--modern-environment=retail") parsed.options.environment = false;
                else return std::unexpected("--modern-environment requires paris or retail");
            }
            else parsed.remaining.push_back(argument);
        }
        return parsed;
    }

    const ModernSceneDefinition& modernSceneDefinition(ModernSceneKind kind) noexcept
    {
        switch (kind)
        {
        case ModernSceneKind::ParisBoard: return ParisBoard;
        case ModernSceneKind::House: return House;
        }
        return Unknown;
    }

    bool qualifiedModernSceneSequence(ModernSceneKind kind, DataId meshId,
        std::optional<DataId> rootSequenceDataId, std::uint16_t priority,
        const ModernSceneOptions& options, const ModernSceneContext& context) noexcept
    {
        if (kind != ModernSceneKind::ParisBoard && kind != ModernSceneKind::House)
            return false;
        const auto& definition = modernSceneDefinition(kind);
        if (meshId != definition.legacyMeshId || !rootSequenceDataId ||
            *rootSequenceDataId != definition.legacyMeshId)
            return false;
        if (kind == ModernSceneKind::ParisBoard)
            return options.parisBoard && priority == display::Board3DPriority &&
                context.edition == BoardEdition::Europe && context.city == ParisCity &&
                context.language == LanguageId::French && context.currency == EuroCurrency &&
                context.customBoardPath.empty();
        const auto endPriority = static_cast<std::uint32_t>(pieces::BoardHousingPriority) +
            static_cast<std::uint32_t>(rules::SquareCount) * pieces::HouseSlotCount;
        return options.house && priority >= pieces::BoardHousingPriority && priority < endPriority;
    }

    std::optional<ModernGltfLoadOptions> modernSceneLoadOptions(ModernSceneKind kind) noexcept
    {
        if (kind == ModernSceneKind::ParisBoard)
        {
            ModernGltfLoadOptions options;
            options.unitsPerMeter = 200.0F;
            options.yawDegrees = 0.0F;
            options.localOffset = {2430.0F, 0.0F, 2430.0F};
            options.groundToZero = false;
            return options;
        }
        if (kind != ModernSceneKind::House) return std::nullopt;
        ModernGltfLoadOptions options;
        options.unitsPerMeter = static_cast<float>(RetailHouseHeight / AuthoredHouseHeightMetres);
        options.yawDegrees = 0.0F;
        options.localOffset = {0.0F, 0.0F, 0.0F};
        options.groundToZero = true;
        return options;
    }
}
