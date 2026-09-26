#pragma once
#include "OptionsUI.hpp"
#include "ResourceRuntime.hpp"
#include <array>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace monopoly::optionsui
{
    inline constexpr std::size_t CustomBoardLimit = 100;
    inline constexpr std::size_t CustomBoardPageSize = 5;
    struct CustomBoardEntry
    {
        std::filesystem::path fileName;
        std::string displayName; // UTF-8, including the .brd extension.
    };
    struct CustomBoardState
    {
        bool active{};
        display::Screen2D previousView{display::Screen2D::PlayerSelect};
        std::filesystem::path directory;
        std::vector<CustomBoardEntry> entries;
        std::size_t pageOffset{};
        int selectedIndex{-1};
        std::array<Rect, 4> buttonRects{}; // Okay, Cancel, Back, Next.
        std::uint64_t revision{};
    };
    struct CustomBoardSelection
    {
        std::filesystem::path boardFile;
        std::filesystem::path assetRoot;
    };
    struct CustomBoardInput
    {
        bool consumed{};
        bool playClick{};
        bool requestLoad{};
        bool closeDialog{};
    };
    [[nodiscard]] Rect customBoardSlotRect(std::size_t slot) noexcept;
    [[nodiscard]] bool customBoardHasPrevious(const CustomBoardState& state) noexcept;
    [[nodiscard]] bool customBoardHasNext(const CustomBoardState& state) noexcept;
    [[nodiscard]] std::expected<void, std::string> openCustomBoardDialog(
        CustomBoardState& state, const std::filesystem::path& moduleDirectory,
        display::Screen2D previousView);
    void closeCustomBoardDialog(CustomBoardState& state) noexcept;
    [[nodiscard]] CustomBoardInput processCustomBoardInput(
        CustomBoardState& state, const uimsg::Message& message) noexcept;
    // Original 32-bit game's HKLM REG_BINARY Version, never a generated value.
    [[nodiscard]] std::expected<std::uint32_t, std::string> readCustomBoardSecurityVersion();
    // Explicit verifier also permits fixture tests without modifying the registry.
    [[nodiscard]] std::expected<CustomBoardSelection, std::string> validateCustomBoardFile(
        const std::filesystem::path& directory, const std::filesystem::path& fileName,
        std::uint32_t installedSecurityVersion);
    [[nodiscard]] std::expected<CustomBoardSelection, std::string> validateSelectedCustomBoard(
        const CustomBoardState& state);
    // SetUpLoadedGame restores the saved asset directory, without requiring
    // the original .brd ownership file or editor registry. A missing first
    // camera means the board was removed and requests the retail stock fallback.
    // Other errors fail before the caller publishes any loaded-game state.
    [[nodiscard]] std::expected<std::optional<std::filesystem::path>, std::string>
    restoreSavedCustomBoard(std::string_view savedAssetRoot,
        const data::ResourceSnapshot& resources, int monetarySystem);
}
