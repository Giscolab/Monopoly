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
}
