#pragma once

#include "ModernGltfMesh.hpp"
#include "SequenceRenderData.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>
#include <vector>

namespace monopoly::engine
{
    inline constexpr std::size_t ModernEnvironmentCount = 3;
    // Logical identities belong only to the optional native modern scene.
    // They are not retail bank/sequence IDs and never enter retail resolution.
    inline constexpr std::uint16_t ModernEnvironmentLogicalGroup = 0xFFFB;
    inline constexpr sequence::SequenceNodeId ModernEnvironmentNodeBase = 0x8000FFFB00000000ULL;

    struct ModernEnvironmentDefinition
    {
        std::string_view slug;
        std::string_view relativeGlbPath;
        data::DataId logicalId{};
        sequence::SequenceNodeId node{};
        std::array<float, 3> originalYup{};
        float localYawDegrees{-90.0F};
    };
    [[nodiscard]] const std::array<ModernEnvironmentDefinition, ModernEnvironmentCount>&
        modernEnvironmentDefinitions() noexcept;

    class ModernEnvironment final
    {
    public:
        using Diagnostic = std::function<void(std::string_view)>;
        using Loader = std::function<std::expected<std::shared_ptr<const data::MeshRenderData>,
            data::MeshRuntimeError>(const std::filesystem::path&, data::ModernGltfLoadOptions)>;

        ModernEnvironment() = default;
        explicit ModernEnvironment(std::filesystem::path modernAssetsRoot,
            bool enabled = false, Diagnostic diagnostic = {}, Loader loader = {});
        void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
        [[nodiscard]] bool enabled() const noexcept { return enabled_; }
        // Renderer rejection permanently drops this owner's optional geometry.
        // Pointer identity prevents unrelated/native-retail instances being rejected.
        void rejectGeometry(const data::MeshRenderData* geometry);

        // Caller must qualify the active board as the actual modern Paris mesh.
        // The matrix is its real sequencer transform, including the retail .10
        // scale. Geometry remains immutable; only these scene instances move.
        [[nodiscard]] std::vector<sequence::SequenceMeshRenderItem> items(
            const sequence::Matrix3D& boardMatrix, std::uint32_t tick);

    private:
        void loadOnce(std::size_t index);
        void report(std::string_view message) const;
        std::filesystem::path root_;
        bool enabled_{};
        bool invalidMatrixReported_{};
        Diagnostic diagnostic_;
        Loader loader_;
        std::array<bool, ModernEnvironmentCount> attempted_{};
        std::array<std::shared_ptr<const data::MeshRuntimeAsset>, ModernEnvironmentCount> assets_{};
    };
}
