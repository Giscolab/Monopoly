#include "RuleConfiguration.hpp"

#include "RuleArchive.hpp"

#include <utility>

namespace monopoly::rules::configuration
{
    void clearAcceptConfiguration(GameState& state) noexcept
    {
        // RULE_PLAYERSVOTEONRULES == 0 in the shipped source: everyone is
        // pre-approved, then the host (first local human in the original)
        // must confirm. The modern rules runtime is local-only, so the first
        // non-AI player is the same host approximation already used here.
        for (PlayerState& player : state.players)
            player.acceptedConfiguration = true;

        for (PlayerNumber playerNo = 0; playerNo < state.numberOfPlayers; ++playerNo)
        {
            if (state.players[playerNo].aiPlayerLevel == 0)
            {
                state.players[playerNo].acceptedConfiguration = false;
                break;
            }
        }
    }

    AcceptanceUpdate applyAcceptedConfiguration(
        GameState& state,
        const actions::Message& message)
    {
        AcceptanceUpdate update{};
        GameOptions received = state.options;

        // Rule.cpp defaults to the current options when the RIFF payload is
        // absent or invalid. decodeOptions is transactional, so a failed
        // decode intentionally leaves `received` unchanged.
        (void)archive::decodeOptions(message.binaryDataA, received);

        if (message.numberC < 1)
        {
            received.futureRentTradingAllowed = false;
            received.immunitiesTradingAllowed = false;
        }

        if (received != state.options)
        {
            update.optionsChanged = true;

            if (message.numberD == 0)
                clearAcceptConfiguration(state);

            state.configurationProposer = message.fromPlayer;
            state.options = received;
        }

        if (message.numberD == 0 &&
            !state.players[message.fromPlayer].acceptedConfiguration)
        {
            state.players[message.fromPlayer].acceptedConfiguration = true;
            update.acceptanceChanged = true;
        }

        return update;
    }

    bool acceptedConfigurationMessage(
        const GameOptions& options,
        PlayerNumber fromPlayer,
        bool interim,
        actions::Message& result)
    {
        if (fromPlayer >= MaxPlayers)
            return false;

        actions::Message message{};
        message.action = actions::Type::AcceptConfiguration;
        message.fromPlayer = fromPlayer;
        message.toPlayer = BankPlayer;
        message.numberC = 1;
        message.numberD = interim ? 1 : 0;

        if (!archive::encodeOptions(options, message.binaryDataA))
            return false;

        result = std::move(message);
        return true;
    }


    actions::Message proposedConfigurationMessage(const GameState& state)
    {
        actions::Message message{};
        message.action = actions::Type::NotifyProposedConfiguration;
        message.fromPlayer = BankPlayer;
        message.toPlayer = AllPlayers;
        message.numberA = state.configurationProposer;

        std::uint32_t playerSet = 0;
        for (PlayerNumber playerNo = 0; playerNo < state.numberOfPlayers; ++playerNo)
        {
            if (!state.players[playerNo].acceptedConfiguration)
                playerSet |= (1u << playerNo);
        }

        message.numberB = playerSet;
        message.numberC = 1;
        (void)archive::encodeOptions(state.options, message.binaryDataA);
        return message;
    }
}
