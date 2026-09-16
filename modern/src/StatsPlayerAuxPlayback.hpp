#pragma once

#include "StatsPlayerPlayback.hpp"
#include "StatsFutureImmunityUI.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace monopoly::statsui
{
    inline constexpr std::uint16_t PlayerAuxPriority = 510;
    inline constexpr data::DataTag PlayerJailUsBaseTag = 0x0992;
    inline constexpr data::DataTag PlayerJailEuropeBaseTag = 0x0C60;
    inline constexpr data::DataTag PlayerFutureUsTag = 0x1027;
    inline constexpr data::DataTag PlayerFutureEuropeTag = 0x12FB;
    inline constexpr data::DataTag PlayerImmunityUsTag = 0x1053;
    inline constexpr data::DataTag PlayerImmunityEuropeTag = 0x1327;

    class PlayerAuxPlayback final
    {
    public:
        struct Published
        {
            data::DataId id{data::EmptyDataId};
            std::uint16_t priority{};
            int x{};
            int y{};
            std::optional<FutureImmunityIcon> icon;
            friend bool operator==(const Published&, const Published&) = default;
        };
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state, const rules::GameState& gameState,
            const PlayerPlaybackInputs& inputs,
            display::Screen2D desiredView,
            engine::SequencePlayback& playback);

        void reset() noexcept { current_.clear(); }
        [[nodiscard]] std::size_t objectCount() const noexcept
        {
            return current_.size();
        }
        [[nodiscard]] std::vector<FutureImmunityIcon> iconHits() const;

    private:
        std::vector<Published> current_;
    };
}
