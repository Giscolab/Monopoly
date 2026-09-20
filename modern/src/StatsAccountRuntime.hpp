#pragma once

#include "Actions.hpp"
#include "ResourceRuntime.hpp"
#include "RuleTypes.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace monopoly::statsui
{
    struct AccountHistoryRow
    {
        rules::PlayerNumber player{rules::NobodyPlayer};
        std::uint32_t turn{};
        std::u16string description;
        bool operator==(const AccountHistoryRow&) const = default;
    };

    struct AccountState
    {
        std::uint64_t dividendCount{};
        std::uint64_t bankErrorCount{};
        std::uint32_t turn{};
        int scrollLines{};
        int scrollLimit{};
        std::vector<AccountHistoryRow> history;
    };

    [[nodiscard]] std::expected<std::u16string, std::string> statsPropertyName(
        const data::LanguageCatalog& catalog, data::BoardEdition edition,
        int city, int square);

    // Owns acchist.txt. Rows use explicit LE32 player/turn and 500 UTF-16LE
    // units, reproducing the Windows wide ACHAR record without native structs.
    class AccountRuntime final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> open(
            std::filesystem::path historyPath);
        [[nodiscard]] std::expected<void, std::string> resetGame();
        // Call before applying the notification to the UI projection so the
        // previous house count remains available, like UDIBar.cpp.
        [[nodiscard]] std::expected<void, std::string> processRuleMessage(
            const actions::Message& message, const rules::GameState& previous,
            rules::PlayerNumber iBarPlayer, int city,
            const data::ResourceSnapshot& resources);
        void recordDividend() noexcept { ++state_.dividendCount; }
        void recordBankError() noexcept { ++state_.bankErrorCount; }
        void scroll(int lines) noexcept;
        void setScrollLimit(int lines) noexcept;
        [[nodiscard]] const AccountState& state() const noexcept { return state_; }
    private:
        std::filesystem::path path_;
        AccountState state_;
    };
}
