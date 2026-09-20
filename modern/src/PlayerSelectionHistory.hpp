#pragma once

#include <array>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace monopoly::playerselection
{
    struct HistoryEntry
    {
        std::wstring name;
        int wins{};
        int greatestNetWorth{};
    };
    struct HistoryPlayer
    {
        std::wstring name;
        int aiLevel{};
        bool local{};
    };
    class PlayerSelectionHistory final
    {
    public:
        [[nodiscard]] std::expected<void,std::string> open(std::filesystem::path path);
        [[nodiscard]] std::expected<void,std::string> gameStarted(std::span<const HistoryPlayer> players);
        [[nodiscard]] std::expected<void,std::string> gameOver(std::wstring_view name,int worth,int aiLevel);
        [[nodiscard]] std::vector<HistoryEntry> highScores() const;
        [[nodiscard]] std::vector<std::wstring> names() const;
        [[nodiscard]] const std::array<HistoryEntry,100>& entries() const noexcept {return entries_;}
        [[nodiscard]] bool configured() const noexcept {return !path_.empty();}
    private:
        [[nodiscard]] std::expected<void,std::string> save(const std::array<HistoryEntry,100>& entries);
        std::filesystem::path path_;
        std::array<HistoryEntry,100> entries_{};
        std::string preserved_;
        bool winnerProcessed_{};
    };
}
