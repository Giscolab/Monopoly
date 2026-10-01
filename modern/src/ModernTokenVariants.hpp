#pragma once

#include "ModernGltfMesh.hpp"

#include <array>
#include <expected>
#include <filesystem>
#include <memory>
#include <map>
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
    [[nodiscard]] const std::array<ModernTokenVariantDefinition, 6>&
    horseIdleVariantDefinitions() noexcept;
    enum class ModernTokenVariantKind { ShipMovement, DogIdle, HorseIdle };
    struct ModernTokenVariantRootDefinition
    {
        DataId rootSequenceDataId{};
        std::span<const DataId> requiredMeshes;
        bool idlePriority{};
    };
    [[nodiscard]] std::span<const ModernTokenVariantRootDefinition>
    modernTokenVariantRootDefinitions() noexcept;
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
        friend class ModernTokenVariantCache;
        std::vector<ModernTokenVariantDefinition> definitions_;
        std::vector<std::shared_ptr<const MeshRenderData>> meshes_;
    };

    // Owner-thread cache. Root subsets publish transactionally, sharing lazy
    // immutable HMD geometry. Root and geometry failures are remembered until
    // owner reset; a failed root never publishes a partial modern subset.
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
        // Presentation failure invalidates all roots requiring the shared state,
        // including future roots, before retail views are republished.
        // Disjoint root subsets remain available.
        [[nodiscard]] bool rejectPack(const MeshRenderData* failedState);
    private:
        std::filesystem::path root_;
        friend class ModernTokenVariantPack;
        struct Entry
        {
            bool attempted{};
            std::shared_ptr<const ModernTokenVariantPack> pack;
            std::optional<MeshRuntimeError> error;
        };
        struct GeometryEntry
        {
            bool attempted{};
            // Retain rejected ownership so an address cannot be reused.
            std::shared_ptr<const MeshRenderData> mesh;
            std::optional<MeshRuntimeError> error;
        };
        [[nodiscard]] std::expected<std::shared_ptr<const ModernTokenVariantPack>, MeshRuntimeError>
        loadRoot(DataId rootSequenceDataId);
        [[nodiscard]] std::expected<std::shared_ptr<const MeshRenderData>, MeshRuntimeError>
        loadGeometry(DataId meshId);
        std::map<DataId, Entry> packs_;
        std::map<DataId, GeometryEntry> geometry_;
        std::optional<float> shipGroundBaseline_;
    };
}
