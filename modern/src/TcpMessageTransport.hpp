#pragma once

#include "MessageTransport.hpp"

#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace monopoly::messaging
{
    // Explicit numeric IPv4 endpoint; no DNS or blocking connection setup in
    // the render loop. Host accepts spectator voice clients, not remote players.
    // This protocol is native to Modern and is not DirectPlay wire-compatible.
    [[nodiscard]] std::expected<std::unique_ptr<Transport>, std::string>
    openTcpTransport(bool host, std::string_view address, std::uint16_t port);
}
