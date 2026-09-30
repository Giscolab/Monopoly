#pragma once

#include "ModernGltfMesh.hpp"

#include <array>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace monopoly::data
{
    struct ModernTokenVariantDefinition
    {
        DataId legacyMeshId{};
        // Relative to the runtime assets/modern directory.
        std::string_view relativeGlbPath;
    };

    [[nodiscard]] const std::array<ModernTokenVariantDefinition, 2>&
    shipMovementVariantDefinitions() noexcept;
    [[nodiscard]] const std::array<ModernTokenVariantDefinition, 4>&
    dogIdleVariantDefinitions() noexcept;
    enum class ModernTokenVariantKind { ShipMovement, DogIdle };
    [[nodiscard]] bool qualifiedModernTokenVariantSequence(DataId meshId,
        std::optional<DataId> rootSequenceDataId, std::uint16_t priority) noexcept;

    // Immutable, transactionally loaded complete pack for one reviewed root. A
    // malformed or missing state publishes no pack, including its good state.
    // CNK remains the owner of mesh selection, clocks, transforms and visibility.
    class ModernTokenVariantPack final
    {
    public:
        [[nodiscard]] static std::expected<std::shared_ptr<const ModernTokenVariantPack>, MeshRuntimeError>
        load(const std::filesystem::path& modernAssetsRoot,
            ModernTokenVariantKind kind = ModernTokenVariantKind::ShipMovement);
        [[nodiscard]] std::shared_ptr<const MeshRenderData> resolve(DataId meshId) const noexcept;
    private:
        std::span<const ModernTokenVariantDefinition> definitions_;
        std::vector<std::shared_ptr<const MeshRenderData>> meshes_;
    };

    // Owner-thread cache. A failure is remembered and makes every state of
    // this root fall back to retail; loading is attempted once per cache owner.
    class ModernTokenVariantCache final
    {
    public:
        explicit ModernTokenVariantCache(std::filesystem::path modernAssetsRoot);
        [[nodiscard]] std::shared_ptr<const MeshRenderData> resolve(DataId meshId,
            std::optional<DataId> rootSequenceDataId, std::uint16_t priority);
        [[nodiscard]] bool attempted() const noexcept;
        [[nodiscard]] bool attempted(DataId rootSequenceDataId) const noexcept;
        [[nodiscard]] const std::optional<MeshRuntimeError>& loadError() const noexcept;
        [[nodiscard]] const std::optional<MeshRuntimeError>& loadError(DataId rootSequenceDataId) const noexcept;
        // Presentation upload failure for any state invalidates its whole pack
        // before the owning renderer republishes its retail views.
        [[nodiscard]] bool rejectPack(const MeshRenderData* failedState);
    private:
        std::filesystem::path root_;
        struct Entry
        {
            bool attempted{};
            std::shared_ptr<const ModernTokenVariantPack> pack;
            std::optional<MeshRuntimeError> error;
        };
        std::array<Entry, 2> packs_;
    };
}
