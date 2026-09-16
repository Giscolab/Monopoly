#pragma once

#include "OptionsUI.hpp"
#include "RuleTypes.hpp"
#include "UIMessages.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace monopoly::data
{
    class ResourceSnapshot;
}

namespace monopoly::fonts
{
    class Runtime;
}

namespace monopoly::optionsui
{
    inline constexpr std::size_t SaveSlotCount = 5;

    enum class FileDialogMode : std::uint8_t
    {
        None,
        Load,
        Save
    };
    struct SaveMetadata
    {
        std::u16string description;
        std::int32_t city{};
        std::int32_t system{};
        std::array<std::uint32_t, rules::SquareCount> squareGameEarnings{};
        std::string customBoardName;
    };

    struct SaveSlot
    {
        bool occupied{};
        SaveMetadata metadata;
    };

    struct SaveRuntimeState
    {
        FileDialogMode dialog{FileDialogMode::None};
        std::array<SaveSlot, SaveSlotCount> slots{};
        int selectedSlot{-1};
        std::u16string draftDescription;
        std::optional<std::size_t> pendingSaveSlot;
        SaveMetadata pendingMetadata;
        std::uint64_t revision{};
    };

    struct SaveDialogInput
    {
        bool consumed{};
        bool requestLoad{};
        bool requestSave{};
        bool closeDialog{};
        bool playClick{};
    };

    [[nodiscard]] std::filesystem::path saveGameDirectory();
    [[nodiscard]] std::filesystem::path gameBlobPath(std::size_t slot);
    [[nodiscard]] std::filesystem::path gameMetadataPath(std::size_t slot);

    [[nodiscard]] std::expected<void, std::string> refreshSaveSlots(
        SaveRuntimeState& state, FileDialogMode mode);
    void closeSaveDialog(SaveRuntimeState& state) noexcept;

    [[nodiscard]] SaveDialogInput processSaveDialogInput(
        SaveRuntimeState& state,
        const uimsg::Message& message,
        fonts::Runtime* fontRuntime,
        std::shared_ptr<const data::ResourceSnapshot> resources);

    [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string>
        readSelectedGameBlob(const SaveRuntimeState& state);

    [[nodiscard]] std::expected<void, std::string> beginPendingSave(
        SaveRuntimeState& state,
        const rules::GameState& ruleState,
        int city,
        int system,
        std::string customBoardName);

    [[nodiscard]] std::expected<void, std::string> persistPendingSave(
        SaveRuntimeState& state,
        std::span<const std::uint8_t> gameBlob);

    [[nodiscard]] std::expected<void, std::string> applySelectedMetadata(
        const SaveRuntimeState& state,
        rules::GameState& ruleState,
        int& city,
        int& system);

    [[nodiscard]] Rect saveSlotRect(std::size_t slot) noexcept;
    [[nodiscard]] Rect dialogOkayRect(int width, int height) noexcept;
    [[nodiscard]] Rect dialogCancelRect(int width, int height) noexcept;
}
