#pragma once

#include "AuctionUI.hpp"
#include "DataBanks.hpp"
#include "RuleTypes.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace monopoly::fonts { class Runtime; }

namespace monopoly::auctionui
{
    class TextPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            const rules::GameState& gameState,
            display::Screen2D desiredView,
            int monetarySystem,
            fonts::Runtime* fontRuntime,
            engine::SequencePlayback& playback);

        void reset() noexcept;

    private:
        struct Published
        {
            data::DataId id{data::EmptyDataId};
            std::uint16_t priority{};
            std::int32_t x{};
            std::int32_t y{};
            bool operator==(const Published&) const = default;
        };

        [[nodiscard]] std::expected<void, std::string> ensureSurfaces(
            engine::SequencePlayback& playback);

        std::optional<data::DataId> currentBidSurface_;
        std::array<std::optional<data::DataId>, rules::MaxPlayers> nameSurfaces_{};
        std::array<std::optional<data::DataId>, rules::MaxPlayers> bidSurfaces_{};
        std::array<std::optional<data::DataId>, rules::MaxPlayers> cashSurfaces_{};
        std::vector<Published> currentObjects_;
        std::optional<std::string> currentBidCache_;
        std::array<std::optional<std::string>, rules::MaxPlayers> nameCache_{};
        std::array<std::optional<std::string>, rules::MaxPlayers> bidCache_{};
        std::array<std::optional<std::string>, rules::MaxPlayers> cashCache_{};
        std::optional<std::u16string> currentBidLabel_;
    };
}
