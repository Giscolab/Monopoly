#pragma once

#include "Actions.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace monopoly::messaging
{
    inline constexpr std::size_t MessageQueueCapacity =
        (rules::MaxPlayers * 3) +
        (3 * rules::SquareCount);

    bool initialize();
    void shutdown();

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

    bool serverMode();
    bool networkMode();

    std::size_t queuedActionCount();
}


