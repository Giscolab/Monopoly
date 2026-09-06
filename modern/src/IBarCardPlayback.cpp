#include "IBarCardPlayback.hpp"

#include <utility>
#include <variant>
#include <vector>

namespace monopoly::ibar
{
    namespace
    {
        [[nodiscard]] bool sequenceFinished(
            engine::SequencePlayback& playback,
            data::DataId id)
        {
            if (id == data::EmptyDataId)
                return false;
            const auto info = playback.runtime().info(id, CardPriority, false);
            return info && info->sequenceClock >= info->endTime;
        }

        [[nodiscard]] std::int32_t cardY(display::Screen2D view) noexcept
        {
            return view == display::Screen2D::Main ? CardYMain : CardYNotMain;
        }

        [[nodiscard]] std::uint8_t endingAction(CardVisualState state) noexcept
        {
            return state == CardVisualState::Out
                ? CardEndingStop : CardEndingStayAtEnd;
        }
        [[nodiscard]] data::DataId languageCardSequence(
            CardVisualState state,
            std::uint8_t cardIndex) noexcept
        {
            const bool community = cardIndex >= CardsPerDeck;
            const auto local = static_cast<data::DataTag>(
                community ? cardIndex - CardsPerDeck : cardIndex);
            data::DataTag base = 0;
            switch (state)
            {
            case CardVisualState::CardIn:
                base = community ? CommunityCardInBaseTag : ChanceCardInBaseTag;
                break;
            case CardVisualState::FaceIn:
                base = community ? CommunityFaceBaseTag : ChanceFaceBaseTag;
                break;
            case CardVisualState::Idle:
                base = community ? CommunityIdleBaseTag : ChanceIdleBaseTag;
                break;
            case CardVisualState::Out:
                base = community ? CommunityCardOutBaseTag : ChanceCardOutBaseTag;
                break;
            default:
                return data::EmptyDataId;
            }
            return data::packDataId(
                data::LegacyGroupId::LanguageGraphics,
                static_cast<data::DataTag>(base + local));
        }
    }

    data::DataId cardSequence(
        CardVisualState state,
        std::uint8_t cardIndex,
        pieces::BoardCameraView camera) noexcept
    {
        if (cardIndex >= CardsPerDeck * 2)
            return data::EmptyDataId;

        if (state == CardVisualState::DeckOut)
        {
            const auto cameraIndex = static_cast<data::DataTag>(camera);
            const auto base = cardIndex < CardsPerDeck
                ? ChanceDeckOutBaseTag : CommunityDeckOutBaseTag;
            return data::packDataId(
                data::LegacyGroupId::Main,
                static_cast<data::DataTag>(base + cameraIndex));
        }

        return languageCardSequence(state, cardIndex);
    }


    std::expected<void, std::string> CardPlayback::sync(
        std::optional<std::uint8_t> desiredCard,
        bool iBarVisible,
        display::Screen2D view,
        pieces::BoardCameraView camera,
        engine::SequencePlayback& playback)
    {
        if (desiredCard && *desiredCard >= CardsPerDeck * 2)
            return std::unexpected("IBar card index is outside legacy 0..31 range");
        auto nextState = visualState_;
        auto nextLastCard = lastCard_;
        switch (visualState_)
        {
        case CardVisualState::Off:
            if (desiredCard && iBarVisible)
            {
                nextLastCard = desiredCard;
                nextState = CardVisualState::DeckOut;
            }
            break;
        case CardVisualState::DeckOut:
            if (!iBarVisible)
                nextState = CardVisualState::FaceIn;
            else if (sequenceFinished(playback, currentSequence_))
                nextState = CardVisualState::CardIn;
            break;
        case CardVisualState::CardIn:
            if (!iBarVisible || sequenceFinished(playback, currentSequence_))
                nextState = CardVisualState::FaceIn;
            break;
        case CardVisualState::FaceIn:
            if (!iBarVisible || sequenceFinished(playback, currentSequence_))
                nextState = CardVisualState::Idle;
            break;
        case CardVisualState::Idle:
            if (!iBarVisible || desiredCard != lastCard_)
                nextState = CardVisualState::Out;
            break;
        case CardVisualState::Out:
            nextState = CardVisualState::Off;
            break;
        }

        if (nextState == visualState_)
            return {};

        // Legacy case 4 forgets the outgoing ID immediately; its Stop ending
        // action is responsible for removing the sequence later.
        if (nextState == CardVisualState::Off)
        {
            visualState_ = CardVisualState::Off;
            currentSequence_ = data::EmptyDataId;
            return {};
        }

        if (!nextLastCard)
            return std::unexpected("IBar card transition has no remembered card");

        const data::DataId desiredSequence = cardSequence(
            nextState, *nextLastCard, camera);
        if (desiredSequence == data::EmptyDataId)
            return std::unexpected("IBar card transition resolved no sequence");
        auto loaded = sequence::SequenceProgram::load(
            playback.resources(), desiredSequence);
        if (!loaded)
            return std::unexpected(loaded.error().detail);

        sequence::ClockStartOptions options{};
        options.dropFrames = true;
        std::vector<sequence::SequenceCommand> commands;
        if (currentSequence_ != data::EmptyDataId)
        {
            commands.push_back(sequence::StopSequenceCommand{
                currentSequence_, CardPriority, false});
        }
        commands.push_back(sequence::StartSequenceCommand{
            std::move(*loaded), CardPriority, options});
        commands.push_back(sequence::makeMoveXY(
            desiredSequence, CardPriority, CardX, cardY(view)));
        commands.push_back(sequence::SetSequenceEndingActionCommand{
            desiredSequence, CardPriority, endingAction(nextState), false});

        if (commands.size() > sequence::SequenceCommandQueue::Capacity -
            playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit IBar card transition");
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
                return std::unexpected(
                    "validated IBar card command rejected");
        }

        visualState_ = nextState;
        lastCard_ = nextLastCard;
        currentSequence_ = desiredSequence;
        return {};
    }


    void CardPlayback::reset() noexcept
    {
        visualState_ = CardVisualState::Off;
        currentSequence_ = data::EmptyDataId;
        lastCard_.reset();
    }
}
