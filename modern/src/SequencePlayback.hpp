#pragma once

#include "SequenceCommands.hpp"
#include "SequenceWorld3DSlot.hpp"

namespace monopoly::engine
{
    // One playback session owns one resource generation. Published assets and
    // descriptions keep that generation alive through resource-service shutdown.
    class SequencePlayback final
    {
    public:
        explicit SequencePlayback(std::shared_ptr<const data::ResourceSnapshot> resources)
            : meshes_(std::move(resources)), commands_(runtime_) {}

        [[nodiscard]] std::expected<void, std::string> start(
            data::DataId id, std::uint16_t priority = 0);
        [[nodiscard]] std::expected<void, std::string> startMoved(
            data::DataId id, std::uint16_t priority,
            sequence::SequenceTransform transform);
        [[nodiscard]] std::expected<void, std::string> stop(
            data::DataId id, std::uint16_t priority);
        [[nodiscard]] std::expected<void, std::string> setCamera3D(
            const World3DCamera& camera);
        [[nodiscard]] std::expected<void, std::string> setCameraNumber(
            std::uint8_t cameraNumber);
        [[nodiscard]] std::expected<void, std::string> update(std::int32_t tick);
        sequence::SequenceCommandQueue& commands() noexcept { return commands_; }
        sequence::SequenceRuntime& runtime() noexcept { return runtime_; }
        SequenceWorld3DSlot& world() noexcept { return world_; }
    private:
        data::MeshRuntimeCache meshes_;
        sequence::SequenceRuntime runtime_;
        sequence::SequenceCommandQueue commands_;
        SequenceWorld3DSlot world_;
    };
}
