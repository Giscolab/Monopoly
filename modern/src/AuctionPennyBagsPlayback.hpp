#pragma once

#include "AuctionUI.hpp"
#include "DataBanks.hpp"
#include "RuleTypes.hpp"
#include "SequencePlayback.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>

namespace monopoly::auctionui
{
    inline constexpr std::uint16_t AuctionPennyBagsPriority = 325;
    inline constexpr std::uint8_t AuctionPennyBagsEndingStayAtEnd = 2;

    inline constexpr data::DataTag PennyBagsAn01Tag = 0x000E;
    inline constexpr data::DataTag PennyBagsAn02Tag = 0x000F;
    inline constexpr data::DataTag PennyBagsAn03Tag = 0x0010;
    inline constexpr data::DataTag PennyBagsAn04Tag = 0x0011;
    inline constexpr data::DataTag PennyBagsAn05Tag = 0x0012;
    inline constexpr data::DataTag PennyBagsAn06Tag = 0x0013;
    inline constexpr data::DataTag PennyBagsAn07Tag = 0x0014;
    inline constexpr data::DataTag PennyBagsAn08Tag = 0x0015;
    inline constexpr data::DataTag PennyBagsAn09Tag = 0x0016;
    inline constexpr data::DataTag PennyBagsAn10Tag = 0x0017;
    inline constexpr data::DataTag PennyBagsAn11Tag = 0x0018;
    inline constexpr data::DataTag PennyBagsAn12Tag = 0x0019;
    inline constexpr data::DataTag PennyBagsAn13Tag = 0x001A;
    inline constexpr data::DataTag PennyBagsAn14Tag = 0x001B;
    inline constexpr data::DataTag PennyBagsAn15Tag = 0x001C;
    inline constexpr data::DataTag PennyBagsAn16Tag = 0x001D;
    inline constexpr data::DataTag PennyBagsAn17Tag = 0x001E;

    struct PennyBagsUpdate
    {
        std::optional<display::Screen2D> requestedBackdrop;
    };

    using AuctionReadySender = std::function<std::expected<void, std::string>(
        std::uint32_t playerMask,
        std::int64_t serial)>;

    [[nodiscard]] std::expected<data::DataId, std::string> pennyBagsSequenceDataId(
        PennyBagsState state,
        std::uint8_t variant = 0);

    class PennyBagsPlayback final
    {
    public:
        [[nodiscard]] std::expected<PennyBagsUpdate, std::string> sync(
            State& state,
            const rules::GameState& gameState,
            display::Screen2D desiredView,
            engine::SequencePlayback& playback,
            const AuctionReadySender& readySender);

        void reset() noexcept;

        [[nodiscard]] data::DataId currentSequence() const noexcept
        {
            return currentSequence_;
        }
        [[nodiscard]] data::DataId desiredSequence() const noexcept
        {
            return desiredSequence_;
        }
        [[nodiscard]] bool heardIntro() const noexcept
        {
            return heardIntro_;
        }
        [[nodiscard]] bool animationReachedEnd() const noexcept
        {
            return animationReachedEnd_;
        }

    private:
        data::DataId currentSequence_{data::EmptyDataId};
        data::DataId desiredSequence_{data::EmptyDataId};
        bool animationReachedEnd_{true};
        // UDAuct.cpp keeps this as a static process-lifetime flag; reset() must
        // not make later auctions replay the first-auction explanation.
        bool heardIntro_{};
    };
}
