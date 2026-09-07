#pragma once

#include "DataBanks.hpp"
#include "IBarLayout.hpp"
#include "RuleTypes.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::ibar
{
    inline constexpr data::DataTag ScoreJailBarsTag = 0x01BF;
    inline constexpr data::DataTag ScoreTokenBaseTag = 0x01C0;
    inline constexpr data::DataTag ScoreLargeColourBaseTag = 0x01CB;
    inline constexpr data::DataTag ScoreSmallColourBaseTag = 0x01D1;
    inline constexpr std::uint16_t ScoreGeneralPriority = 256;
    inline constexpr std::uint16_t ScoreBoxPriority = 305;
    inline constexpr std::uint8_t ScorePlayerColourCount = 6;

    struct ScoreStripPlayerPlan
    {
        bool visible{};
        data::DataId token{data::EmptyDataId};
        data::DataId colourBar{data::EmptyDataId};
        bool jailBars{};
        int x{};
        int width{};
        bool hovered{};
        std::int64_t cash{};
        std::wstring name;
    };

    struct ScoreStripPlan
    {
        std::array<ScoreStripPlayerPlan, rules::MaxPlayers> players{};
        std::uint32_t tick{};
    };

    struct ScoreStripInputs
    {
        std::array<bool, rules::MaxPlayers> visiblePlayers{};
        bool gameInProgress{};
        int hoveredPlayer{-1};
        std::uint32_t tick{};
    };

    [[nodiscard]] std::expected<ScoreStripPlan, std::string> planScoreStrip(
        const rules::GameState& state,
        const ScoreStripInputs& inputs);

    enum class ScoreCashChange : std::uint8_t
    {
        None,
        Up,
        Down
    };

    struct ScoreTextState
    {
        std::int64_t displayedCash{-1};
        std::wstring printedName;
        std::uint32_t lastCashUpdateTick{};
        ScoreCashChange lastCashChange{ScoreCashChange::None};
        bool redrawRequested{};
    };

    struct ScoreStripPlayerRuntime
    {
        bool visible{};
        data::DataId token{data::EmptyDataId};
        data::DataId colourBar{data::EmptyDataId};
        bool jailBars{};
        int x{};
        bool hovered{};
    };

    class ScoreStripPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const ScoreStripPlan& plan,
            engine::SequencePlayback& playback);

        void reset() noexcept
        {
            players_ = {};
            text_ = {};
            for (auto& state : text_) state.displayedCash = -1;
        }

        [[nodiscard]] const ScoreTextState& textState(
            rules::PlayerNumber player) const noexcept
        {
            return text_[static_cast<std::size_t>(player)];
        }

        [[nodiscard]] const ScoreStripPlayerRuntime& playerState(
            rules::PlayerNumber player) const noexcept
        {
            return players_[static_cast<std::size_t>(player)];
        }

    private:
        std::array<ScoreStripPlayerRuntime, rules::MaxPlayers> players_{};
        std::array<ScoreTextState, rules::MaxPlayers> text_ = []
        {
            std::array<ScoreTextState, rules::MaxPlayers> states{};
            for (auto& state : states) state.displayedCash = -1;
            return states;
        }();
    };
}
