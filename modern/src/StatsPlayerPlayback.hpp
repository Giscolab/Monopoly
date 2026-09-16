#pragma once

#include "IBarLayout.hpp"
#include "IBarRuleState.hpp"
#include "SequencePlayback.hpp"
#include "StatsUI.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace monopoly::statsui
{
    inline constexpr std::uint16_t PlayerBoxPriority = 500;
    inline constexpr std::uint16_t PlayerDeedBasePriority = 510;
    inline constexpr data::DataTag PlayerBoxLargeBaseTag = 0x0349;
    inline constexpr data::DataTag PlayerBoxSmallBaseTag = 0x034F;
    inline constexpr data::DataTag PlayerDeedMortgagedBaseTag = 0x05CA;
    inline constexpr data::DataTag PlayerDeedNormalBaseTag = 0x05E6;
    inline constexpr int PlayerDeedWidth = 36;
    inline constexpr int PlayerDeedHeight = 42;

    struct PlayerPlaybackInputs
    {
        ibar::RuleMode mode{ibar::RuleMode::Nothing};
        rules::PlayerNumber iBarPlayer{rules::NobodyPlayer};
        bool iBarPlayerLocalHuman{};
        ibar::layout::PropertyMask buildProperties{};
        ibar::layout::PropertyMask sellProperties{};
        ibar::layout::PropertyMask mortgageProperties{};
    };

    class PlayerPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state, const rules::GameState& gameState,
            const PlayerPlaybackInputs& inputs,
            display::Screen2D desiredView, engine::SequencePlayback& playback);
        void reset() noexcept { current_.clear(); }
        [[nodiscard]] std::size_t objectCount() const noexcept { return current_.size(); }

        struct Published
        {
            data::DataId id{data::EmptyDataId};
            std::uint16_t priority{};
            int x{};
            int y{};
            friend bool operator==(const Published&, const Published&) = default;
        };

    private:
        std::vector<Published> current_;
    };
}
