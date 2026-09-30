#include "ModernTokenVariants.hpp"

#include "ModernTokenCatalog.hpp"
#include "PieceRuntime.hpp"

#include <cmath>
#include <exception>
#include <string>
#include <utility>

namespace monopoly::data
{
    namespace
    {
        constexpr DataId ShipMovementRoot = packDataId(LegacyGroupId::ThreeD, 0x0360);
        constexpr std::array<ModernTokenVariantDefinition, 2> ShipVariants{{
            {packDataId(LegacyGroupId::ThreeD, 0x0018), "tokens/ship_variants/rest.glb"},
            {packDataId(LegacyGroupId::ThreeD, 0x001B), "tokens/ship_variants/squash.glb"}}};
        constexpr DataId DogIdleRoot = packDataId(LegacyGroupId::ThreeD, 0x01D3);
        constexpr std::array<ModernTokenVariantDefinition, 4> DogVariants{{
            {packDataId(LegacyGroupId::ThreeD, 0x0040), "tokens/dog_variants/idle_0040.glb"},
            {packDataId(LegacyGroupId::ThreeD, 0x0041), "tokens/dog_variants/idle_0041.glb"},
            {packDataId(LegacyGroupId::ThreeD, 0x0042), "tokens/dog_variants/idle_0042.glb"},
            {packDataId(LegacyGroupId::ThreeD, 0x0043), "tokens/dog_variants/idle_0043.glb"}}};
        const std::optional<MeshRuntimeError> NoError;

        std::optional<ModernTokenVariantKind> kindForRoot(std::optional<DataId> root) noexcept
        {
            if (root == ShipMovementRoot) return ModernTokenVariantKind::ShipMovement;
            if (root == DogIdleRoot) return ModernTokenVariantKind::DogIdle;
            return std::nullopt;
        }
        std::span<const ModernTokenVariantDefinition> definitionsFor(ModernTokenVariantKind kind) noexcept
        {
            if (kind == ModernTokenVariantKind::ShipMovement) return ShipVariants;
            if (kind == ModernTokenVariantKind::DogIdle) return DogVariants;
            return {};
        }
    }

    const std::array<ModernTokenVariantDefinition, 2>& shipMovementVariantDefinitions() noexcept
    { return ShipVariants; }
    const std::array<ModernTokenVariantDefinition, 4>& dogIdleVariantDefinitions() noexcept
    { return DogVariants; }

    bool qualifiedModernTokenVariantSequence(DataId meshId,
        std::optional<DataId> rootSequenceDataId, std::uint16_t priority) noexcept
    {
        const auto kind = kindForRoot(rootSequenceDataId);
        if (!kind) return false;
        if (*kind == ModernTokenVariantKind::ShipMovement && priority != pieces::Generic3DPriority)
            return false;
        if (*kind == ModernTokenVariantKind::DogIdle &&
            (priority < pieces::TokenPriority || priority >= pieces::TokenPriority + rules::MaxPlayers))
            return false;
        for (const auto& state : definitionsFor(*kind))
            if (state.legacyMeshId == meshId) return true;
        return false;
    }

    std::expected<std::shared_ptr<const ModernTokenVariantPack>, MeshRuntimeError>
    ModernTokenVariantPack::load(const std::filesystem::path& modernAssetsRoot, ModernTokenVariantKind kind) try
    {
        const auto* ship = modernTokenDefinition(6);
        ModernGltfLoadOptions options;
        options.unitsPerMeter = ship->unitsPerMeter;
        options.yawDegrees = ship->yawDegrees;
        options.localOffset = ship->localOffset;
        options.groundToZero = false;
        if (kind == ModernTokenVariantKind::DogIdle)
        {
            // Frozen shared authoring frame, already grounded once at export.
            // Individual dog pose minima must remain above/below zero.
            options.unitsPerMeter = 154.80F;
            options.yawDegrees = 90.0F;
            options.localOffset = {-0.5F, 0.0F, -15.05967734F};
        }
        auto staged = std::make_shared<ModernTokenVariantPack>();
        staged->definitions_ = definitionsFor(kind);
        if (staged->definitions_.empty())
            return std::unexpected(MeshRuntimeError{MeshRuntimeErrorCode::ModernAssetInvalid,
                "unknown modern token variant pack"});
        staged->meshes_.resize(staged->definitions_.size());
        for (std::size_t index = 0; index < staged->definitions_.size(); ++index)
        {
            const auto path = modernAssetsRoot / staged->definitions_[index].relativeGlbPath;
            auto mesh = loadModernGltfMesh(path, options);
            if (!mesh)
            {
                auto error = mesh.error();
                error.detail = std::string(staged->definitions_[index].relativeGlbPath) + ": " + error.detail;
                return std::unexpected(std::move(error));
            }
            staged->meshes_[index] = std::move(*mesh);
        }
        const auto& rest = *staged->meshes_[0];
        for (const auto& state : staged->meshes_)
            if (rest.vertices.size() != state->vertices.size() || rest.indices != state->indices)
                return std::unexpected(MeshRuntimeError{MeshRuntimeErrorCode::ModernAssetInvalid,
                    "token variant pack requires the same authored vertex/triangle correspondence"});
        if (kind == ModernTokenVariantKind::DogIdle)
            return std::shared_ptr<const ModernTokenVariantPack>(std::move(staged));

        // Both states share the REST grounding baseline. Independently
        // grounding squash would destroy the authored relative deformation.
        const float baseline = rest.bounds.minimum[1];
        if (!std::isfinite(baseline))
            return std::unexpected(MeshRuntimeError{MeshRuntimeErrorCode::ModernAssetInvalid,
                "ship variant pack has an invalid shared grounding baseline"});
        for (auto& mesh : staged->meshes_)
        {
            auto grounded = std::make_shared<MeshRenderData>(*mesh);
            for (auto& vertex : grounded->vertices)
            {
                vertex.position[1] -= baseline;
                if (!std::isfinite(vertex.position[1]))
                    return std::unexpected(MeshRuntimeError{MeshRuntimeErrorCode::ModernAssetInvalid,
                        "ship variant shared grounding exceeds finite coordinates"});
            }
            grounded->bounds.minimum[1] -= baseline;
            grounded->bounds.maximum[1] -= baseline;
            mesh = std::move(grounded);
        }
        return std::shared_ptr<const ModernTokenVariantPack>(std::move(staged));
    }
    catch (const std::exception& error)
    {
        return std::unexpected(MeshRuntimeError{MeshRuntimeErrorCode::ModernAssetInvalid,
            std::string("token variant pack loading failed: ") + error.what()});
    }

    std::shared_ptr<const MeshRenderData> ModernTokenVariantPack::resolve(DataId meshId) const noexcept
    {
        for (std::size_t index = 0; index < definitions_.size(); ++index)
            if (meshId == definitions_[index].legacyMeshId) return meshes_[index];
        return {};
    }

    ModernTokenVariantCache::ModernTokenVariantCache(std::filesystem::path modernAssetsRoot)
        : root_(std::move(modernAssetsRoot)) {}

    std::shared_ptr<const MeshRenderData> ModernTokenVariantCache::resolve(DataId meshId,
        std::optional<DataId> rootSequenceDataId, std::uint16_t priority)
    {
        if (!qualifiedModernTokenVariantSequence(meshId, rootSequenceDataId, priority)) return {};
        const auto kind = *kindForRoot(rootSequenceDataId);
        auto& entry = packs_[static_cast<std::size_t>(kind)];
        if (!entry.attempted)
        {
            entry.attempted = true;
            auto loaded = ModernTokenVariantPack::load(root_, kind);
            if (loaded) entry.pack = std::move(*loaded);
            else entry.error = std::move(loaded.error());
        }
        return entry.pack ? entry.pack->resolve(meshId) : nullptr;
    }

    bool ModernTokenVariantCache::attempted() const noexcept
    { return packs_[0].attempted || packs_[1].attempted; }
    bool ModernTokenVariantCache::attempted(DataId rootSequenceDataId) const noexcept
    {
        const auto kind = kindForRoot(rootSequenceDataId);
        return kind && packs_[static_cast<std::size_t>(*kind)].attempted;
    }
    const std::optional<MeshRuntimeError>& ModernTokenVariantCache::loadError() const noexcept
    {
        for (const auto& entry : packs_) if (entry.error) return entry.error;
        return NoError;
    }
    const std::optional<MeshRuntimeError>& ModernTokenVariantCache::loadError(DataId rootSequenceDataId) const noexcept
    {
        const auto kind = kindForRoot(rootSequenceDataId);
        return kind ? packs_[static_cast<std::size_t>(*kind)].error : NoError;
    }

    bool ModernTokenVariantCache::rejectPack(const MeshRenderData* failedState)
    {
        if (!failedState) return false;
        for (std::size_t index = 0; index < packs_.size(); ++index)
        {
            auto& entry = packs_[index];
            if (!entry.pack) continue;
            for (const auto& state : definitionsFor(static_cast<ModernTokenVariantKind>(index)))
            {
                if (entry.pack->resolve(state.legacyMeshId).get() == failedState)
                {
                    entry.pack.reset();
                    entry.error = MeshRuntimeError{MeshRuntimeErrorCode::ModernAssetInvalid,
                        "token variant pack rejected after presentation upload failure"};
                    return true;
                }
            }
        }
        return false;
    }
}
