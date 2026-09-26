#pragma once

#include "Actions.hpp"
#include "MessageTransport.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <functional>
#include <string_view>

namespace monopoly::messaging
{
    inline constexpr std::size_t MessageQueueCapacity =
        (rules::MaxPlayers * 3) +
        (3 * rules::SquareCount);

    bool initialize();
    void shutdown();

    // Call after local startup and before the first game cycle. A connecting
    // client is never a RULE server, even before/after its connection is live.
    bool startNetwork(std::unique_ptr<Transport> transport);
    void pumpNetwork();
    void stopNetwork();
    void setNetworkStarter(std::function<bool()> starter);
    [[nodiscard]] bool startConfiguredNetwork();
    [[nodiscard]] bool gameplayNetwork();
    [[nodiscard]] bool gameplayReady();
    void noteClientResynchronized();
    [[nodiscard]] bool consumeHostDisconnected();
    void resetPlayerOwners();
    void setPlayerOwner(rules::PlayerNumber player, std::uint32_t source);
    [[nodiscard]] std::uint32_t playerOwner(rules::PlayerNumber player);
    [[nodiscard]] bool authenticSender(const rules::GameState& state,
        const actions::Message& message);
    [[nodiscard]] std::string_view networkError();

    bool sendAction(const actions::Message& message);

    bool sendAction(
        actions::Type action,
        rules::PlayerNumber fromPlayer,
        rules::PlayerNumber toPlayer,
        std::int64_t numberA = 0,
        std::int64_t numberB = 0,
        std::int64_t numberC = 0,
        std::int64_t numberD = 0,
        std::wstring_view stringA = {}
    );

    void clearActionQueue();

    std::size_t currentQueueSize();
    bool receiveAction(actions::Message& message);
    bool receiveVoiceChatOnly(actions::Message& message);

    bool serverMode();
    bool networkMode();

    std::size_t queuedActionCount();
}


