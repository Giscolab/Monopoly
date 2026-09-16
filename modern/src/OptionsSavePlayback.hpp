#pragma once

#include "OptionsSaveRuntime.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace monopoly::fonts
{
    class Runtime;
}

namespace monopoly::optionsui
{
    class SavePlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const SaveRuntimeState& state,
            display::Screen2D desiredView,
            fonts::Runtime* fontRuntime,
            engine::SequencePlayback& playback);

        void reset() noexcept;
    private:
        [[nodiscard]] std::expected<void, std::string> ensureTextSurfaces(
            fonts::Runtime& fontRuntime,
            engine::SequencePlayback& playback);
        [[nodiscard]] std::expected<void, std::string> refreshTextSurfaces(
            const SaveRuntimeState& state,
            fonts::Runtime& fontRuntime,
            engine::SequencePlayback& playback);
        [[nodiscard]] std::expected<void, std::string> show(
            const SaveRuntimeState& state,
            fonts::Runtime& fontRuntime,
            engine::SequencePlayback& playback);
        [[nodiscard]] std::expected<void, std::string> hide(
            engine::SequencePlayback& playback);
        [[nodiscard]] std::expected<void, std::string> refreshSelection(
            const SaveRuntimeState& state,
            engine::SequencePlayback& playback);

        bool visible_{};
        FileDialogMode mode_{FileDialogMode::None};
        int selectedSlot_{-1};
        std::uint64_t revision_{};
        std::optional<data::DataId> title_;
        std::array<data::DataId, SaveSlotCount> slotBackgrounds_{};
        std::array<std::optional<data::DataId>, SaveSlotCount> slotText_{};
        std::optional<data::DataId> okayText_;
        std::optional<data::DataId> cancelText_;
    };
}
