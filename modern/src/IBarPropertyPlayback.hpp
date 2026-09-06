#pragma once

#include "DataBanks.hpp"
#include "IBarLayout.hpp"
#include "IBarRuleState.hpp"
#include "RuleTypes.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace monopoly::ibar
{
    inline constexpr data::DataTag PropertyFullColourBaseTag = 0x0163;
    inline constexpr data::DataTag PropertyLowColourBaseTag = 0x017F;
    inline constexpr data::DataTag PropertyMortgagedBaseTag = 0x019B;
    inline constexpr std::uint16_t PropertyBasePriority = 256;
    inline constexpr data::DataTag PropertyHoverMortgagedBaseTag = 0x0B53;
    inline constexpr data::DataTag PropertyHoverNormalBaseTag = 0x0CD0;
    inline constexpr std::uint16_t PropertyHoverPriority = 1003;
    inline constexpr std::int32_t PropertyHoverX = 540;
    inline constexpr std::int32_t PropertyHoverY = 130;
    inline constexpr std::uint64_t PropertyHoverDelayTicks = 36;

    enum class PropertyTitleStyle : std::uint8_t
    {
        Hidden = 0,
        FullColour,
        LowColour,
        Mortgaged
    };

    struct PropertyTitleInputs
    {
        bool available{};
        rules::PlayerNumber player{rules::NobodyPlayer};
        RuleMode mode{RuleMode::Nothing};
        RuleMode projectedMode{RuleMode::Nothing};
        layout::PropertyMask buildProperties{};
        layout::PropertyMask sellProperties{};
        layout::PropertyMask mortgageProperties{};
        layout::PropertyMask unmortgageProperties{};
        layout::PropertyMask freeUnmortgageProperties{};
        layout::PropertyMask placeBuildingProperties{};
        std::optional<std::uint8_t> selectedDeed;
    };

    struct PropertyTitlePlan
    {
        std::array<PropertyTitleStyle, rules::SquareCount> styles{};
        layout::PropertyMask visibleProperties{};
    };

    [[nodiscard]] PropertyTitlePlan planPropertyTitles(
        const rules::GameState& state,
        const PropertyTitleInputs& inputs) noexcept;

    [[nodiscard]] data::DataId propertyTitleDataId(
        int square, PropertyTitleStyle style) noexcept;

    class PropertyTitlePlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const PropertyTitlePlan& plan,
            engine::SequencePlayback& playback);
        void reset() noexcept;

    private:
        std::array<data::DataId, rules::SquareCount> current_{};
        std::array<std::uint16_t, rules::SquareCount> priorities_{};
    };

    [[nodiscard]] data::DataId propertyHoverDataId(
        int square, bool mortgaged) noexcept;

    class PropertyHoverPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const rules::GameState& state,
            const PropertyTitlePlan& titles,
            int currentMouseOver,
            std::uint64_t tick,
            engine::SequencePlayback& playback);
        void reset() noexcept;

        [[nodiscard]] data::DataId currentDeed() const noexcept
        {
            return currentDeed_;
        }
        [[nodiscard]] int checkedSquare() const noexcept { return checkedSquare_; }
        [[nodiscard]] std::uint64_t hoverStartTick() const noexcept { return hoverStartTick_; }

    private:
        int checkedSquare_{-1};
        std::uint64_t hoverStartTick_{};
        data::DataId currentDeed_{data::EmptyDataId};
    };

}
