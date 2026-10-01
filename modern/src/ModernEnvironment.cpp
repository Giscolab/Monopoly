#include "ModernEnvironment.hpp"

#include "Display.hpp"
#include "SequenceTransforms.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace monopoly::engine
{
    namespace
    {
        constexpr std::array<ModernEnvironmentDefinition, ModernEnvironmentCount> Definitions{{
            {"fountain", "environment/paris_fountain.glb",
                data::packDataId(ModernEnvironmentLogicalGroup, 0), ModernEnvironmentNodeBase | 1U,
                {0.0F, 0.12F, -2.0F}, -90.0F},
            {"station", "environment/paris_station.glb",
                data::packDataId(ModernEnvironmentLogicalGroup, 1), ModernEnvironmentNodeBase | 2U,
                {-5.5F, 0.12F, -5.9F}, -90.0F},
            {"morris", "environment/paris_morris_column.glb",
                data::packDataId(ModernEnvironmentLogicalGroup, 2), ModernEnvironmentNodeBase | 3U,
                {3.9F, 0.0F, -4.1F}, -90.0F}
        }};
        constexpr std::array<ModernEnvironmentDefinition, 2> PresentationDefinitions{{
            {"table", "presentation/table.glb", data::packDataId(ModernEnvironmentLogicalGroup, 3),
                ModernEnvironmentNodeBase | 4U, {0,0,0}, 0.0F},
            {"plinth", "presentation/plinth.glb", data::packDataId(ModernEnvironmentLogicalGroup, 4),
                ModernEnvironmentNodeBase | 5U, {0,0,0}, 0.0F}
        }};
        const ModernEnvironmentDefinition& definitionAt(std::size_t index)
        { return index < Definitions.size() ? Definitions[index] : PresentationDefinitions[index-Definitions.size()]; }
        constexpr float SourceUnitsPerMetre = 200.0F;
        constexpr float BoardOrigin = 2430.0F;

        bool validGeometry(const data::MeshRenderData& mesh) noexcept
        {
            if (mesh.vertices.empty() || mesh.indices.empty() || mesh.batches.empty() ||
                mesh.indices.size() % 3U != 0U) return false;
            for (std::size_t axis = 0; axis < 3; ++axis)
                if (!std::isfinite(mesh.bounds.minimum[axis]) ||
                    !std::isfinite(mesh.bounds.maximum[axis]) ||
                    mesh.bounds.minimum[axis] > mesh.bounds.maximum[axis]) return false;
            for (const auto& vertex : mesh.vertices)
                for (std::size_t axis = 0; axis < 3; ++axis)
                    if (!std::isfinite(vertex.position[axis]) ||
                        !std::isfinite(vertex.normal[axis])) return false;
            for (const auto index : mesh.indices)
                if (index >= mesh.vertices.size()) return false;
            for (const auto& batch : mesh.batches)
                if (batch.material.model != data::MeshMaterialModel::MetallicRoughness ||
                    !batch.indexCount || batch.indexCount % 3U != 0U ||
                    batch.firstIndex > mesh.indices.size() ||
                    batch.indexCount > mesh.indices.size() - batch.firstIndex) return false;
            return true;
        }
    }

    const std::array<ModernEnvironmentDefinition, ModernEnvironmentCount>&
        modernEnvironmentDefinitions() noexcept
    { return Definitions; }

    ModernEnvironment::ModernEnvironment(std::filesystem::path modernAssetsRoot,
        bool enabled, Diagnostic diagnostic, Loader loader)
        : root_(std::move(modernAssetsRoot)), enabled_(enabled),
          diagnostic_(std::move(diagnostic)), loader_(std::move(loader)) {}

    void ModernEnvironment::report(std::string_view message) const
    {
        if (diagnostic_) diagnostic_(message);
        else std::cerr << "Modern environment: " << message << '\n';
    }

    void ModernEnvironment::loadOnce(std::size_t index)
    {
        if (attempted_[index]) return;
        attempted_[index] = true;
        const auto& definition = definitionAt(index);
        data::ModernGltfLoadOptions options;
        options.unitsPerMeter = SourceUnitsPerMetre;
        // Center placement follows the aligned board mapping; the decoration
        // itself uses a proper rotation so text/front faces are never mirrored.
        options.yawDegrees = definition.localYawDegrees;
        options.localOffset = {};
        options.groundToZero = false;
        try
        {
            const auto path = root_ / definition.relativeGlbPath;
            auto loaded = loader_ ? loader_(path, options) : data::loadModernGltfMesh(path, options);
            if (!loaded || !*loaded || !validGeometry(**loaded))
            {
                report(std::string(definition.slug) + ": skipped " +
                    (loaded ? "invalid native geometry" : loaded.error().detail));
                return;
            }
            auto asset = std::make_shared<data::MeshRuntimeAsset>();
            asset->dataId = definition.logicalId;
            asset->origin = data::MeshAssetOrigin::ModernGltf;
            asset->renderData = std::move(*loaded);
            assets_[index] = std::move(asset);
        }
        catch (const std::bad_alloc&)
        {
            report("skipped decoration allocation failure");
        }
    }

    void ModernEnvironment::rejectGeometry(const data::MeshRenderData* geometry)
    {
        if (!geometry) return;
        bool dropped{};
        for (std::size_t index = 0; index < assets_.size(); ++index)
            if (assets_[index] && assets_[index]->renderData.get() == geometry)
            {
                assets_[index].reset();
                attempted_[index] = true;
                dropped = true;
            }
        if (dropped) report("dropped rejected native GPU geometry");
    }

    std::vector<sequence::SequenceMeshRenderItem> ModernEnvironment::items(
        const sequence::Matrix3D& boardMatrix, std::uint32_t tick, bool includePresentation, bool includeLandmarks)
    {
        std::vector<sequence::SequenceMeshRenderItem> result;
        if (!enabled_) return result;
        if (!std::all_of(boardMatrix.values.begin(), boardMatrix.values.end(),
                [](float value) { return std::isfinite(value); }))
        {
            if (!invalidMatrixReported_)
            {
                invalidMatrixReported_ = true;
                report("skipped invalid board transform");
            }
            return result;
        }
        const auto count = Definitions.size() + (includePresentation ? PresentationDefinitions.size() : 0);
        result.reserve(count);
        for (std::size_t index = includeLandmarks ? 0 : Definitions.size(); index < count; ++index)
        {
            loadOnce(index);
            if (!assets_[index]) continue;
            const auto& definition = definitionAt(index);
            const auto& position = definition.originalYup;
            const auto local = sequence::translate3D(
                BoardOrigin - position[2] * SourceUnitsPerMetre,
                position[1] * SourceUnitsPerMetre,
                BoardOrigin - position[0] * SourceUnitsPerMetre);
            sequence::SequenceMeshRenderItem item;
            item.node = definition.node;
            item.contentsDataId = definition.logicalId;
            // No retail sequence root owns these native scene items.
            item.rootSequenceDataId = data::EmptyDataId;
            item.priority = display::Board3DPriority;
            item.clock = static_cast<std::int32_t>(std::min(tick,
                static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())));
            // Row vectors: local placement precedes the actual board transform.
            // Reversing these operands would leave the 2430 origin unscaled.
            item.worldTransform = sequence::multiply(local, boardMatrix);
            item.asset = assets_[index];
            item.renderData = assets_[index]->renderData;
            result.push_back(std::move(item));
        }
        return result;
    }
}
