#include "IBarCardPlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;

    void require(bool condition, const char* message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) throw std::runtime_error(message);
    }

    const engine::SequenceWorld2DObject* onlyObject(
        engine::SequencePlayback& playback)
    {
        if (playback.world2D().size() != 1) return nullptr;
        return playback.world2D().find(playback.world2D().order().front());
    }

    void testIdentifiers()
    {
        require(data::dataGroup(ibar::cardSequence(
                    ibar::CardVisualState::DeckOut, 0,
                    pieces::BoardCameraView::TopDownSquare)) ==
                data::legacyGroupValue(data::LegacyGroupId::Main),
            "deck fly-off sequences live in DAT_MAIN");
        require(data::dataTag(ibar::cardSequence(
                    ibar::CardVisualState::DeckOut, 0,
                    pieces::BoardCameraView::TopDownSquare)) == 0x000F &&
                data::dataTag(ibar::cardSequence(
                    ibar::CardVisualState::DeckOut, 16,
                    pieces::BoardCameraView::TopDownSquare)) == 0x0036,
            "Chance and Community deck fly-off bases match CNK_cnck01/cnyk01");

        require(data::dataTag(ibar::cardSequence(
                    ibar::CardVisualState::CardIn, 0,
                    pieces::BoardCameraView::TopDownSquare)) == 0x0038 &&
                data::dataTag(ibar::cardSequence(
                    ibar::CardVisualState::FaceIn, 0,
                    pieces::BoardCameraView::TopDownSquare)) == 0x0018 &&
                data::dataTag(ibar::cardSequence(
                    ibar::CardVisualState::Idle, 0,
                    pieces::BoardCameraView::TopDownSquare)) == 0x0028 &&
                data::dataTag(ibar::cardSequence(
                    ibar::CardVisualState::Out, 0,
                    pieces::BoardCameraView::TopDownSquare)) == 0x0048,
            "Chance card in/face/idle/out bases match USA DAT_LANG2");

        require(data::dataTag(ibar::cardSequence(
                    ibar::CardVisualState::CardIn, 16,
                    pieces::BoardCameraView::TopDownSquare)) == 0x0069 &&
                data::dataTag(ibar::cardSequence(
                    ibar::CardVisualState::FaceIn, 16,
                    pieces::BoardCameraView::TopDownSquare)) == 0x0008,
            "Community card in and face bases match USA DAT_LANG2");
        require(data::dataTag(ibar::cardSequence(
                    ibar::CardVisualState::Idle, 16,
                    pieces::BoardCameraView::TopDownSquare)) == 0x0059 &&
                data::dataTag(ibar::cardSequence(
                    ibar::CardVisualState::Out, 16,
                    pieces::BoardCameraView::TopDownSquare)) == 0x0079,
            "Community idle and out bases match USA DAT_LANG2");

        for (std::uint8_t camera = 0;
             camera < static_cast<std::uint8_t>(pieces::BoardCameraView::Count);
             ++camera)
        {
            const auto view = static_cast<pieces::BoardCameraView>(camera);
            require(data::dataTag(ibar::cardSequence(
                        ibar::CardVisualState::DeckOut, 0, view)) ==
                        static_cast<data::DataTag>(0x000F + camera) &&
                    data::dataTag(ibar::cardSequence(
                        ibar::CardVisualState::DeckOut, 16, view)) ==
                        static_cast<data::DataTag>(0x0036 + camera),
                "all 39 legacy board cameras preserve deck fly-off offsets");
        }
    }

    void testChanceLifecycle()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::CardPlayback card;
        constexpr auto camera = pieces::BoardCameraView::TopDownSquare;

        require(card.sync(static_cast<std::uint8_t>(0), true, display::Screen2D::Main, camera, playback) &&
                card.visualState() == ibar::CardVisualState::DeckOut &&
                playback.commands().pendingCount() == 3 && playback.update(0),
            "Chance pickup starts deck fly-off with StartXYDrop/StayAtEnd");
        const auto* deck = onlyObject(playback);
        require(deck && deck->priority == ibar::CardPriority &&
                deck->worldTransform.values[6] == 0.0F &&
                deck->worldTransform.values[7] == 0.0F,
            "Chance deck fly-off preserves priority 1005 and Main StartXY(0,0)");

        require(card.sync(static_cast<std::uint8_t>(0), true, display::Screen2D::Main, camera, playback) &&
                playback.commands().pendingCount() == 0,
            "Chance deck fly-off remains until its finite sequence completes");
        require(playback.update(10).has_value() &&
                card.sync(static_cast<std::uint8_t>(0), true, display::Screen2D::Main, camera, playback) &&
                card.visualState() == ibar::CardVisualState::CardIn &&
                playback.commands().pendingCount() == 4 && playback.update(11),
            "finished Chance deck fly-off transitions to card-in");
        require(data::dataTag(card.currentSequence()) == 0x0038,
            "Chance card-in resolves CNK_cycai01");

        require(playback.update(20).has_value() &&
                card.sync(static_cast<std::uint8_t>(0), true, display::Screen2D::Main, camera, playback) &&
                card.visualState() == ibar::CardVisualState::FaceIn &&
                playback.commands().pendingCount() == 4 && playback.update(21),
            "finished Chance card-in transitions to face animation");
        require(data::dataTag(card.currentSequence()) == 0x0018,
            "Chance face animation resolves CNK_ch01");

        require(playback.update(30).has_value() &&
                card.sync(static_cast<std::uint8_t>(0), true, display::Screen2D::Main, camera, playback) &&
                card.visualState() == ibar::CardVisualState::Idle &&
                playback.commands().pendingCount() == 4 && playback.update(31),
            "finished Chance face transitions to idle card");
        require(data::dataTag(card.currentSequence()) == 0x0028,
            "Chance idle resolves CNK_cycad01");
        require(card.sync(static_cast<std::uint8_t>(0), true, display::Screen2D::Main, camera, playback) &&
                playback.commands().pendingCount() == 0,
            "unchanged Chance card remains idle without restart");

        require(card.sync(std::nullopt, true, display::Screen2D::Main,
                    camera, playback) &&
                card.visualState() == ibar::CardVisualState::Out &&
                playback.commands().pendingCount() == 4 && playback.update(32),
            "CardSeen/PutAway request starts Chance outgoing animation");
        require(data::dataTag(card.currentSequence()) == 0x0048,
            "Chance outgoing resolves CNK_cycao01");

        require(card.sync(std::nullopt, true, display::Screen2D::Main,
                    camera, playback) &&
                card.visualState() == ibar::CardVisualState::Off &&
                card.currentSequence() == data::EmptyDataId &&
                playback.commands().pendingCount() == 0,
            "legacy state 4 forgets outgoing ID and returns to Off next cycle");
        require(playback.update(40).has_value() && playback.world2D().size() == 0,
            "outgoing Stop ending action removes Chance card from Overlay2D");
    }

    void testCommunityLifecycleAndOffset()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::CardPlayback card;
        constexpr auto camera = pieces::BoardCameraView::FifteenTiles12;
        constexpr std::uint8_t cardIndex = 31;

        require(card.sync(cardIndex, true, display::Screen2D::Trade,
                    camera, playback) && playback.update(0),
            "Community pickup starts with final legacy camera view");
        require(data::dataTag(card.currentSequence()) == 0x005C,
            "Community deck fly-off uses CNK_cnyk01 + camera 38");
        const auto* deck = onlyObject(playback);
        require(deck && deck->priority == ibar::CardPriority &&
                deck->worldTransform.values[6] == 0.0F &&
                deck->worldTransform.values[7] == 136.0F,
            "non-Main card graphics preserve the legacy +136 Y offset");

        require(playback.update(10).has_value() &&
                card.sync(cardIndex, true, display::Screen2D::Trade,
                    camera, playback) && playback.update(11) &&
                card.visualState() == ibar::CardVisualState::CardIn &&
                data::dataTag(card.currentSequence()) == 0x0078,
            "Community card 15 enters through CNK_cyyi01 + 15");
        require(playback.update(20).has_value() &&
                card.sync(cardIndex, true, display::Screen2D::Trade,
                    camera, playback) && playback.update(21) &&
                card.visualState() == ibar::CardVisualState::FaceIn &&
                data::dataTag(card.currentSequence()) == 0x0017,
            "Community card 15 reaches CNK_cc01 + 15 face animation");
        require(playback.update(30).has_value() &&
                card.sync(cardIndex, true, display::Screen2D::Trade,
                    camera, playback) && playback.update(31) &&
                card.visualState() == ibar::CardVisualState::Idle &&
                data::dataTag(card.currentSequence()) == 0x0068,
            "Community card 15 reaches CNK_cyyd01 + 15 idle");

        const auto* idle = onlyObject(playback);
        require(idle && idle->worldTransform.values[7] == 136.0F,
            "Community in/face/idle keep non-Main Y offset 136");
        require(card.sync(std::nullopt, true, display::Screen2D::Trade,
                    camera, playback) && playback.update(32) &&
                card.visualState() == ibar::CardVisualState::Out &&
                data::dataTag(card.currentSequence()) == 0x0088,
            "Community card 15 exits through CNK_cyyo01 + 15");
        const auto* out = onlyObject(playback);
        require(out && out->worldTransform.values[7] == 136.0F,
            "Community outgoing animation keeps non-Main Y offset 136");
    }

    void testFailureIsTransactional()
    {
        engine::SequencePlayback missing(nullptr);
        ibar::CardPlayback missingCard;
        const auto unavailable = missingCard.sync(
            static_cast<std::uint8_t>(0), true, display::Screen2D::Main,
            pieces::BoardCameraView::TopDownSquare, missing);
        require(!unavailable &&
                missingCard.visualState() == ibar::CardVisualState::Off &&
                missingCard.currentSequence() == data::EmptyDataId &&
                missing.commands().pendingCount() == 0,
            "missing deck resource queues no partial card transition");

        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::CardPlayback card;
        for (std::size_t count = 0;
             count < sequence::SequenceCommandQueue::Capacity - 2; ++count)
        {
            if (!playback.commands().enqueue(
                    sequence::StopSequenceCommand{1, 0, false}))
                throw std::runtime_error("FIFO setup failed");
        }
        const auto noRoom = card.sync(static_cast<std::uint8_t>(0), true, display::Screen2D::Main,
            pieces::BoardCameraView::TopDownSquare, playback);
        require(!noRoom &&
                card.visualState() == ibar::CardVisualState::Off &&
                card.currentSequence() == data::EmptyDataId,
            "insufficient FIFO preserves complete card playback state");

        engine::SequencePlayback invalidPlayback(resources.service.snapshot());
        ibar::CardPlayback invalidCard;
        const auto invalid = invalidCard.sync(static_cast<std::uint8_t>(32), true, display::Screen2D::Main,
            pieces::BoardCameraView::TopDownSquare, invalidPlayback);
        require(!invalid && invalidCard.visualState() == ibar::CardVisualState::Off &&
                invalidPlayback.commands().pendingCount() == 0,
            "card index outside legacy 0..31 range is rejected transactionally");
    }
}

int main()
{
    try
    {
        testIdentifiers();
        testChanceLifecycle();
        testCommunityLifecycleAndOffset();
        testFailureIsTransactional();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
