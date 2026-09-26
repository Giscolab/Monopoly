#pragma once

#include "MessageTransport.hpp"

#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace monopoly::messaging
{
    enum class TcpSessionMode { VoiceSpectators, Gameplay };
    // Explicit numeric IPv4 endpoint; no DNS or blocking connection setup in
    // the render loop. Gameplay admission is finalized by MESS at a safe queue
    // boundary; legacy voice-only callers retain their spectator capability.
    // This protocol is native to Modern and is not DirectPlay wire-compatible.
    [[nodiscard]] std::expected<std::unique_ptr<Transport>, std::string>
    openTcpTransport(bool host, std::string_view address, std::uint16_t port,
        TcpSessionMode mode = TcpSessionMode::VoiceSpectators);
}
