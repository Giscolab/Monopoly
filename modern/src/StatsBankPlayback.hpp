#pragma once

#include "StatsUI.hpp"
#include "SequencePlayback.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace monopoly::statsui
{
    inline constexpr std::uint16_t BankOverlayPriority = 55;
    inline constexpr std::uint16_t BankIconPriority = 56;
    inline constexpr std::uint16_t BankSignPriority = 60;
    inline constexpr int BankContentX = 7;
    inline constexpr int BankContentY = 226;
    inline constexpr int BankDeedWidth = 36;
    inline constexpr int BankDeedHeight = 42;
    inline constexpr data::DataTag BankPlayerBarBaseTag = 0x0001;
    inline constexpr data::DataTag BankHouseTag = 0x0321;
    inline constexpr data::DataTag BankHotelTag = 0x0322;
    inline constexpr data::DataTag BankDeedBaseTag = 0x05E6;
    inline constexpr data::DataTag BankMortgageSignTag = 0x0E3D;
    inline constexpr data::DataTag BankSoldSignTag = 0x0F17;

    [[nodiscard]] std::optional<Rect> bankDeedRect(int square) noexcept;
    [[nodiscard]] data::DataId bankDeedSequence(int square) noexcept;

    class BankPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state, const rules::GameState& gameState,
            display::Screen2D desiredView, engine::SequencePlayback& playback);
        void reset() noexcept { current_.clear(); }
        [[nodiscard]] std::size_t objectCount() const noexcept { return current_.size(); }

        struct Published
        {
            data::DataId id{data::EmptyDataId};
            std::uint16_t priority{};
            int x{};
            int y{};
            bool positioned{};
            friend bool operator==(const Published&, const Published&) = default;
        };

    private:
        std::vector<Published> current_;
    };
}


