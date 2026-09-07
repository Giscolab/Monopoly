#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

namespace monopoly::rules::configuration
{
    struct AcceptanceUpdate
    {
        bool optionsChanged{};
        bool acceptanceChanged{};

        [[nodiscard]] bool restartNeeded() const noexcept
        {
            return optionsChanged || acceptanceChanged;
        }
    };

    void clearAcceptConfiguration(GameState& state) noexcept;

    [[nodiscard]] AcceptanceUpdate applyAcceptedConfiguration(
        GameState& state,
        const actions::Message& message);

    [[nodiscard]] bool acceptedConfigurationMessage(
        const GameOptions& options,
        PlayerNumber fromPlayer,
        bool interim,
        actions::Message& result);

    [[nodiscard]] actions::Message proposedConfigurationMessage(
        const GameState& state);
}
