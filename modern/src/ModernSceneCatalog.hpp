#pragma once

#include "DataBanks.hpp"
#include "ModernGltfMesh.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace monopoly::data
{
    enum class ModernSceneKind : std::uint8_t { ParisBoard, House, Hotel };
    inline constexpr std::size_t ModernSceneKindCount = 3;

    // Optional replacements remain off for ordinary retail play.
    struct ModernSceneOptions
    {
        bool parisBoard{};
        // --modern-buildings=house enables both building prototypes. Hotels
        // still retain retail geometry until their separate asset qualifies.
        bool house{};
        bool environment{};
    };

    struct ParsedModernSceneOptions
    {
        ModernSceneOptions options;
        // Views borrow the caller's arguments; unrelated flags preserve order.
        std::vector<std::string_view> remaining;
    };

    [[nodiscard]] std::expected<ParsedModernSceneOptions, std::string>
    parseModernSceneArguments(std::span<const std::string_view> arguments);

    struct ModernSceneContext
    {
        BoardEdition edition{BoardEdition::Usa};
        int city{};
        LanguageId language{LanguageId::EnglishUs};
        int currency{13};
        std::filesystem::path customBoardPath;
    };

    struct ModernSceneDefinition
    {
        ModernSceneKind kind{};
        DataId legacyMeshId{EmptyDataId};
        std::string_view relativeGlbPath;
    };

    [[nodiscard]] const ModernSceneDefinition& modernSceneDefinition(
        ModernSceneKind kind) noexcept;

    // Only a complete reviewed root and its actual production priority qualify.
    // This is separate from transform calibration: a locale-compatible board
    // must still retain retail geometry until calibrated load options exist.
    [[nodiscard]] bool qualifiedModernSceneSequence(ModernSceneKind kind,
        DataId meshId, std::optional<DataId> rootSequenceDataId,
        std::uint16_t priority, const ModernSceneOptions& options,
        const ModernSceneContext& context) noexcept;

    // nullopt means geometry alignment has not yet been qualified.
    [[nodiscard]] std::optional<ModernGltfLoadOptions> modernSceneLoadOptions(
        ModernSceneKind kind) noexcept;

    // Hotel authoring is an explicit bounds contract, not a measured claim
    // about an asset that may be missing. Gate the loaded prototype before
    // presentation. Existing board and house qualification stays unchanged.
    [[nodiscard]] bool qualifiedModernSceneGeometry(ModernSceneKind kind,
        const MeshRenderData& mesh) noexcept;
}
