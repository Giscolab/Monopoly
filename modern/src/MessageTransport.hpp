#pragma once

#include "Actions.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace monopoly::messaging
{
    // Owned and polled by MESS on the game thread. No callback may touch RULE
    // or the audio backend from a socket thread. The transport assigns
    // sourceId from the connection, never from a client-supplied action field.
    class Transport
    {
    public:
        virtual ~Transport() = default;
        virtual void pump() = 0;
        virtual bool receive(actions::Message& message) = 0;
        virtual bool send(const actions::Message& message) = 0;
        [[nodiscard]] virtual bool server() const noexcept = 0;
        [[nodiscard]] virtual bool active() const noexcept = 0;
        [[nodiscard]] virtual std::size_t queued() const noexcept = 0;
        [[nodiscard]] virtual std::string_view error() const noexcept = 0;
    };
}
