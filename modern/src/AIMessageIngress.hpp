#pragma once

#include "AIProfileRuntime.hpp"
#include "AITradeIngress.hpp"

#include <expected>
#include <filesystem>

namespace monopoly::ai
{
    [[nodiscard]] std::expected<void, profile::Error>
        initializeMessageIngressProfiles(
            const std::filesystem::path& directory);

    void resetMessageIngress() noexcept;

    void processMessage(
        const rules::GameState& state,
        const actions::Message& message) noexcept;

    [[nodiscard]] const trade::TradeIngressState&
        tradeIngressStateReadOnly() noexcept;

    [[nodiscard]] const profile::RuntimeState&
        profileRuntimeStateReadOnly() noexcept;
}
