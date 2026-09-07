#pragma once

#include "IBarLayout.hpp"
#include "IBarRuleState.hpp"
#include "RuleTypes.hpp"
#include "UIMessages.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace monopoly::ibar
{
    struct PlayerDisplay
    {
        bool visible = false;

        bool local = false;

        bool localHuman = false;

        bool localAI = false;


        layout::Rect rect{};
    };


    struct State
    {
        std::array<
            PlayerDisplay,
            rules::MaxPlayers
        > players{};


        int playerLastMouseOver = -1;

        int playerCurrentMouseOver = -1;

        int actionButtonLastMouseOver = -1;
        int actionButtonCurrentMouseOver = -1;
        layout::ActionButtonLayout actionButtonLayout{
            layout::ActionButtonLayout::General};
        layout::ActionButtonMask activeActionButtonSlots{};
        RuleMode actionRuleMode{RuleMode::Nothing};
        rules::PlayerNumber actionPlayer{rules::NobodyPlayer};
        bool actionRemote{};

        int propertyLastMouseOver = -1;
        int propertyCurrentMouseOver = -1;
        layout::PropertyMask visiblePropertySlots{};
        RuleMode projectedRuleMode{RuleMode::Nothing};
        rules::PlayerNumber projectedRulePlayer{rules::NobodyPlayer};
        bool localRuleModeActive{};
        RuleMode localRuleMode{RuleMode::Nothing};
        rules::PlayerNumber localRulePlayer{rules::NobodyPlayer};
        std::optional<std::uint8_t> selectedDeed;
        std::optional<std::uint8_t> pendingPressedButton;
        std::optional<std::uint8_t> desiredCardIndex;


        bool initialized = false;
    };


    bool initialize();


    void shutdown();


    void tickActions(
        std::uint64_t numberOfTicks
    );


    void show();


    void processLibraryMessage(
        const uimsg::Message& message
    );


    void processRuleMessage(
        const actions::Message& message,
        RuleMode projectedMode
    ) noexcept;


    void clearPendingPressedButton(std::uint8_t buttonIndex) noexcept;


    [[nodiscard]] RuleMode resolveRuleMode(
        RuleMode projectedMode,
        rules::PlayerNumber projectedPlayer
    ) noexcept;

    [[nodiscard]] rules::PlayerNumber resolveRulePlayer(
        rules::PlayerNumber projectedPlayer
    ) noexcept;


    void setRuleActionHitState(
        layout::ActionButtonLayout layout,
        layout::ActionButtonMask activeSlots,
        RuleMode mode,
        rules::PlayerNumber player,
        bool remote
    ) noexcept;


    void setPropertyHitState(layout::PropertyMask visibleProperties) noexcept;


    State& state();


    const State& stateReadOnly();
}
