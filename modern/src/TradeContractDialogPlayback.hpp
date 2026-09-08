#pragma once

#include "DataBanks.hpp"
#include "Display.hpp"
#include "SequencePlayback.hpp"
#include "TradeUI.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::tradeui
{
    inline constexpr data::DataTag TradeContractArrowDownTag = 0x0007;
    inline constexpr data::DataTag TradeContractArrowUpTag = 0x0009;
    inline constexpr std::uint16_t TradeContractArrowPriority = 525;

    inline constexpr std::array<std::int32_t, 2> TradeContractArrowX{{22, 22}};
    inline constexpr std::array<std::int32_t, 2> TradeContractArrowY{{-168, -247}};

    [[nodiscard]] constexpr data::DataId tradeContractArrowSequence(
        std::size_t index) noexcept
    {
        const auto tag = index == 0 ? TradeContractArrowUpTag :
            index == 1 ? TradeContractArrowDownTag : 0;
        return tag != 0
            ? data::packDataId(data::LegacyGroupId::Main,
                static_cast<data::DataTag>(tag))
            : data::EmptyDataId;
    }

    class ContractDialogPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            display::Screen2D desiredView,
            engine::SequencePlayback& playback);

        void reset() noexcept { visible_ = false; }
        [[nodiscard]] bool visible() const noexcept { return visible_; }

    private:
        bool visible_{};
    };
}
