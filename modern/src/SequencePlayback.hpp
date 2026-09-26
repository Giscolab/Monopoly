#pragma once

#include "SequenceCommands.hpp"
#include "SequenceWorld3DSlot.hpp"
#include "SequenceWorld2DSlot.hpp"
#include "RuntimeBitmapSurface.hpp"
#include "TextureCatalog.hpp"

#include <array>
#include <vector>
#include <filesystem>
#include <tuple>

namespace monopoly::engine
{
    class SequencePlayback final
    {
    public:
        explicit SequencePlayback(std::shared_ptr<const data::ResourceSnapshot> resources)
            : meshes_(std::move(resources)), commands_(runtime_) {}

        [[nodiscard]] std::expected<void, std::string> start(
            data::DataId id, std::uint16_t priority = 0,
            std::uint8_t labelOverride = 0);
        [[nodiscard]] std::expected<void, std::string> startXY(
            data::DataId id, std::uint16_t priority,
            std::int32_t x, std::int32_t y, bool dropFrames = false,
            std::uint8_t labelOverride = 0);
        [[nodiscard]] std::expected<void, std::string> startXYSR(
            data::DataId id, std::uint16_t priority,
            std::int32_t x, std::int32_t y,
            float scale, float rotate);
        [[nodiscard]] std::expected<void, std::string> transitionXY(
            std::optional<data::DataId> previousId, data::DataId id,
            std::uint16_t priority, std::int32_t x, std::int32_t y,
            bool dropFrames = false);
        [[nodiscard]] std::expected<void, std::string> setEndingAction(
            data::DataId id, std::uint16_t priority, std::uint8_t action);
        [[nodiscard]] std::expected<void, std::string> setVolume(
            data::DataId id, std::uint16_t priority, std::uint8_t volume);
        [[nodiscard]] std::expected<void, std::string> startMoved(
            data::DataId id, std::uint16_t priority,
            sequence::SequenceTransform transform);
        [[nodiscard]] std::expected<void, std::string> stop(
            data::DataId id, std::uint16_t priority);
        [[nodiscard]] std::expected<void, std::string> move(
            data::DataId id, std::uint16_t priority,
            sequence::SequenceTransform transform);
        [[nodiscard]] std::expected<void, std::string> transitionMovedDrop(
            std::optional<data::DataId> previousId, data::DataId id,
            std::uint16_t priority, sequence::SequenceTransform transform,
            std::uint8_t endingAction);
        [[nodiscard]] std::expected<void, std::string> transitionRySTxzDropStayAtEnd(
            std::optional<data::DataId> previousId, data::DataId id,
            std::uint16_t priority, float yaw, float scale, float x, float z);
        [[nodiscard]] std::expected<void, std::string> setCamera3D(
            const World3DCamera& camera);
        [[nodiscard]] std::expected<void, std::string> setCameraNumber(
            std::uint8_t cameraNumber);
        [[nodiscard]] std::expected<void, std::string> update(std::int32_t tick);
        [[nodiscard]] std::expected<void, std::string> configureBoardTextures(
            data::BoardMeshKind mesh, data::TextureResolution resolution,
            int city, int currency, const std::filesystem::path& customRoot = {});
        sequence::SequenceCommandQueue& commands() noexcept { return commands_; }
        sequence::SequenceRuntime& runtime() noexcept { return runtime_; }
        SequenceWorld3DSlot& world() noexcept { return world_; }
        SequenceWorld2DSlot& world2D() noexcept { return world2D_; }
        data::RuntimeBitmapStore& runtimeBitmaps() noexcept { return runtimeBitmaps_; }
        const data::RuntimeBitmapStore& runtimeBitmaps() const noexcept { return runtimeBitmaps_; }
        std::shared_ptr<const data::ResourceSnapshot> resources() const noexcept
        { return meshes_.resources(); }
        // All deed owners share one generated Europe catalog. Static USA IDs
        // remain unchanged; runtime IDs are replaced as one complete set.
        void setEuropeanDeeds(const std::array<data::DataId, 56>& ids);
        [[nodiscard]] data::DataId deedDataId(int square, bool front,
            data::DataId staticFallback) const noexcept;
        [[nodiscard]] std::expected<std::shared_ptr<const sequence::SequenceProgram>, std::string>
            loadProgram(data::DataId id);
    private:
        std::array<data::DataId, 56> europeanDeeds_{};
        std::vector<data::DataId> retiredDeeds_;

        data::MeshRuntimeCache meshes_;
        using BoardTextureSelection = std::tuple<data::BoardMeshKind,
            data::TextureResolution, data::BoardEdition, data::LanguageId, int, int,
            std::filesystem::path>;
        std::optional<BoardTextureSelection> boardTextureSelection_;
        data::RuntimeBitmapStore runtimeBitmaps_;
        sequence::SequenceRuntime runtime_;
        sequence::SequenceCommandQueue commands_;
        SequenceWorld3DSlot world_;
        data::BitmapRuntimeCache bitmaps_;
        SequenceWorld2DSlot world2D_;
    };
}
