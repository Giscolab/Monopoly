#include "AuctionPennyBagsPlayback.hpp"

#include <array>
#include <cstdlib>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::auctionui
{
    namespace
    {
        [[nodiscard]] data::DataId pattern(data::DataTag tag) noexcept
        {
            return data::packDataId(data::LegacyGroupId::Patterns, tag);
        }

        [[nodiscard]] std::uint8_t randomVariant() noexcept
        {
            return static_cast<std::uint8_t>(std::rand() % 3);
        }
    }

    std::expected<data::DataId, std::string> pennyBagsSequenceDataId(
        PennyBagsState state,
        std::uint8_t variant)
    {
        const auto choose = [variant](const std::array<data::DataTag, 3>& tags)
            -> std::expected<data::DataId, std::string>
        {
            if (variant >= tags.size())
                return std::unexpected("auction Pennybags variant is outside legacy 0..2 range");
            return pattern(tags[variant]);
        };

        switch (state)
        {
        case PennyBagsState::Intro:
            return choose({PennyBagsAn01Tag, PennyBagsAn10Tag, PennyBagsAn11Tag});
        case PennyBagsState::Instructions:
            return choose({PennyBagsAn02Tag, PennyBagsAn12Tag, PennyBagsAn13Tag});
        case PennyBagsState::StartBidding:
            return choose({PennyBagsAn14Tag, PennyBagsAn15Tag, PennyBagsAn03Tag});
        case PennyBagsState::NameHighestBidder:
            return pattern(PennyBagsAn04Tag);
        case PennyBagsState::GoingOnce:
            return pattern(PennyBagsAn05Tag);
        case PennyBagsState::GoingTwice:
            return pattern(PennyBagsAn06Tag);
        case PennyBagsState::Sold:
            return pattern(PennyBagsAn07Tag);
        case PennyBagsState::Congrats:
            return choose({PennyBagsAn08Tag, PennyBagsAn16Tag, PennyBagsAn17Tag});
        case PennyBagsState::None:
        case PennyBagsState::Idle:
        case PennyBagsState::Begin:
        case PennyBagsState::EndAuction:
            return data::EmptyDataId;
        }
        return std::unexpected("auction Pennybags state is outside the legacy enum");
    }

    std::expected<PennyBagsUpdate, std::string> PennyBagsPlayback::sync(
        State& state,
        const rules::GameState& gameState,
        display::Screen2D desiredView,
        engine::SequencePlayback& playback,
        const AuctionReadySender& readySender)
    {
        if (gameState.numberOfPlayers > rules::MaxPlayers)
            return std::unexpected("auction Pennybags player count exceeds legacy maximum");

        State planned = state;
        auto nextCurrent = currentSequence_;
        auto nextDesired = desiredSequence_;
        bool nextReachedEnd = animationReachedEnd_;
        bool nextHeardIntro = heardIntro_;
        PennyBagsUpdate update{};
        bool sendReady = false;
        std::uint32_t readyMask{};
        std::int64_t readySerial{};

        const auto chooseSequence = [&](PennyBagsState next)
            -> std::expected<void, std::string>
        {
            auto id = pennyBagsSequenceDataId(next, randomVariant());
            if (!id)
                return std::unexpected(id.error());
            nextDesired = *id;
            return {};
        };

        if (desiredView == display::Screen2D::Auction)
        {
            if (planned.pennyBagsSwitch)
            {
                planned.pennyBagsSwitch = false;
                switch (planned.nextPennyBags)
                {
                case PennyBagsState::Intro:
                    if (nextHeardIntro)
                    {
                        planned.pennyBagsSwitch = true;
                        planned.nextPennyBags = PennyBagsState::StartBidding;
                    }
                    else
                    {
                        nextHeardIntro = true;
                        if (auto selected = chooseSequence(PennyBagsState::Intro); !selected)
                            return std::unexpected(selected.error());
                        planned.nextPennyBags = PennyBagsState::Instructions;
                        // Mirrors AuctionPennyBagsAnimReachedEnd=TRUE before the
                        // new intro ID is committed below.
                        nextReachedEnd = true;
                    }
                    break;

                case PennyBagsState::Instructions:
                    if (auto selected = chooseSequence(PennyBagsState::Instructions); !selected)
                        return std::unexpected(selected.error());
                    planned.nextPennyBags = PennyBagsState::StartBidding;
                    break;

                case PennyBagsState::StartBidding:
                    if (auto selected = chooseSequence(PennyBagsState::StartBidding); !selected)
                        return std::unexpected(selected.error());
                    planned.nextPennyBags = PennyBagsState::Begin;
                    break;

                case PennyBagsState::Begin:
                    if (planned.rollCallSerial != 0)
                    {
                        sendReady = true;
                        readyMask = planned.rollCallPlayers;
                        readySerial = planned.rollCallSerial;
                    }
                    planned.nextPennyBags = PennyBagsState::Idle;
                    break;

                case PennyBagsState::Idle:
                    break;

                case PennyBagsState::NameHighestBidder:
                    if (planned.highestBidder && *planned.highestBidder < rules::MaxPlayers)
                    {
                        if (auto selected = chooseSequence(PennyBagsState::NameHighestBidder); !selected)
                            return std::unexpected(selected.error());
                    }
                    planned.nextPennyBags = PennyBagsState::Idle;
                    break;

                case PennyBagsState::GoingOnce:
                    if (auto selected = chooseSequence(PennyBagsState::GoingOnce); !selected)
                        return std::unexpected(selected.error());
                    planned.nextPennyBags = PennyBagsState::Idle;
                    break;

                case PennyBagsState::GoingTwice:
                    if (auto selected = chooseSequence(PennyBagsState::GoingTwice); !selected)
                        return std::unexpected(selected.error());
                    planned.nextPennyBags = PennyBagsState::Idle;
                    break;

                case PennyBagsState::Sold:
                    if (auto selected = chooseSequence(PennyBagsState::Sold); !selected)
                        return std::unexpected(selected.error());
                    planned.nextPennyBags = planned.highestBid > 0
                        ? PennyBagsState::Congrats
                        : PennyBagsState::EndAuction;
                    break;

                case PennyBagsState::Congrats:
                    if (auto selected = chooseSequence(PennyBagsState::Congrats); !selected)
                        return std::unexpected(selected.error());
                    planned.nextPennyBags = PennyBagsState::EndAuction;
                    break;

                case PennyBagsState::EndAuction:
                    nextDesired = data::EmptyDataId;
                    planned.nextPennyBags = PennyBagsState::None;
                    update.requestedBackdrop = planned.formerView;
                    break;

                case PennyBagsState::None:
                    break;
                }
            }
        }
        else
        {
            nextDesired = data::EmptyDataId;
        }

        // UDAuct.cpp performs this test after processing the requested state and
        // before replacing the displayed ID.  Keep that one-frame handoff exact.
        if (nextCurrent != data::EmptyDataId && !nextReachedEnd)
        {
            const auto info = playback.runtime().info(
                nextCurrent, AuctionPennyBagsPriority, false);
            if (!info)
                return std::unexpected("auction Pennybags displayed sequence has no runtime info");
            if (info->endTime <= info->sequenceClock)
            {
                planned.pennyBagsSwitch =
                    planned.nextPennyBags != PennyBagsState::Idle;
                nextReachedEnd = true;
            }
        }

        std::shared_ptr<const sequence::SequenceProgram> program;
        if (nextDesired != nextCurrent && nextDesired != data::EmptyDataId)
        {
            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), nextDesired);
            if (!loaded)
                return std::unexpected(loaded.error().detail);
            program = std::move(*loaded);
        }

        std::vector<sequence::SequenceCommand> commands;
        if (nextDesired != nextCurrent)
        {
            if (nextCurrent != data::EmptyDataId)
            {
                commands.push_back(sequence::StopSequenceCommand{
                    nextCurrent, AuctionPennyBagsPriority, false});
            }
            if (nextDesired != data::EmptyDataId)
            {
                commands.push_back(sequence::StartSequenceCommand{
                    std::move(program), AuctionPennyBagsPriority, {}});
            }
        }

        if (commands.size() > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit auction Pennybags transition");
        }

        if (sendReady)
        {
            if (!readySender)
                return std::unexpected("auction Pennybags ready sender is unavailable");
            const auto sent = readySender(readyMask, readySerial);
            if (!sent)
                return std::unexpected(sent.error());
            planned.rollCallSerial = 0;
        }

        for (auto& command : commands)
        {
            const auto queued = std::visit(
                [&](auto value)
                {
                    return playback.commands().enqueue(std::move(value));
                },
                std::move(command));
            if (!queued)
                return std::unexpected("validated auction Pennybags command rejected");
        }

        if (nextDesired != nextCurrent)
        {
            nextCurrent = nextDesired;
            nextReachedEnd = nextCurrent == data::EmptyDataId;
        }

        state = std::move(planned);
        currentSequence_ = nextCurrent;
        desiredSequence_ = nextDesired;
        animationReachedEnd_ = nextReachedEnd;
        heardIntro_ = nextHeardIntro;
        return update;
    }

    void PennyBagsPlayback::reset() noexcept
    {
        currentSequence_ = data::EmptyDataId;
        desiredSequence_ = data::EmptyDataId;
        animationReachedEnd_ = true;
        // heardIntro_ intentionally survives: source udauct_DoneAnAuctionBefore
        // is static and is not reset by DISPLAY_UDAUCT_Initialize/Destroy.
    }
}
