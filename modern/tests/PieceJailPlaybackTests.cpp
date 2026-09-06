#include "PieceJailPlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace
{
    using namespace monopoly;
    using namespace monopoly::pieces;
    int failures{};

    void expect(bool value, std::string_view text)
    {
        std::cout << (value ? "[PASS] " : "[FAIL] ") << text << '\n';
        if (!value) ++failures;
    }

    PieceMoveSpecialRequest request(std::int32_t before)
    {
        return {PieceMoveSpecial::GoToJail, 0, 0, before, 40};
    }

    data::DataId threeD(data::DataTag tag)
    { return data::packDataId(data::LegacyGroupId::ThreeD, tag); }

    bool update(engine::SequencePlayback& playback, std::int32_t tick)
    { return playback.update(tick).has_value(); }

    void testFullGoToJailMachine()
    {
        SyntheticSequenceResources fixture;
        engine::SequencePlayback playback(fixture.service.snapshot());
        PieceJailPlayback jail;
        rules::GameState ui{};
        ui.numberOfPlayers = 2;
        ui.players[0].currentSquare = 16;

        expect(jail.begin(request(16), 0, true).has_value(),
            "GoToJail state machine accepts a valid pending special request");
        auto step = jail.tick(0, playback, ui);
        expect(step && jail.state() == 2 && step->camera == BoardCameraView::FifteenTiles03,
            "state 1 builds side-1 outbound route and state 2 starts paddy pop-in");
        expect(update(playback, 0), "state-2 paddy pop-in enters sequence runtime");
        expect(playback.runtime().info(threeD(0x05CA), JailPlaybackPriority, false).has_value(),
            "CNK_mnwpi synthetic sequence is active at priority 77");

        expect(update(playback, 100), "paddy pop-in reaches held end frame");
        step = jail.tick(100, playback, ui);
        expect(step && jail.state() == 3,
            "finished pop-in switches to rolling paddy and enters state 3");
        expect(update(playback, 100), "rolling paddy start/move commands drain");
        auto rolling = playback.runtime().info(threeD(0x05CC), JailPlaybackPriority, false);
        expect(rolling && rolling->sequenceToWorldTransformation,
            "state 3 publishes the rolling paddy with a full world matrix");

        step = jail.tick(190, playback, ui);
        expect(step && jail.state() == 4 &&
            step->camera == pickCameraFor3Squares(17),
            "outbound end advances to door-open and requests the load-square camera");
        expect(update(playback, 190), "door-open sequence starts at outbound endpoint");

        expect(update(playback, 290), "door-open reaches its held final frame");
        step = jail.tick(290, playback, ui);
        expect(step && jail.state() == 5 &&
            jail.playerInPaddywagon() == rules::PlayerNumber{0} &&
            ui.players[0].currentSquare == 40,
            "state 5 hides the moving idle and finally registers square 40");
        expect(update(playback, 290), "open-idle and token-load sequences start together");
        expect(playback.runtime().info(threeD(0x0157), JailPlaybackPriority, false).has_value(),
            "CNK_knawji token animation starts with the source priority");

        expect(update(playback, 390), "token-load animation reaches its end");
        step = jail.tick(390, playback, ui);
        expect(step && jail.state() == 6,
            "state 5 completion starts paddy door-close in state 6");
        expect(update(playback, 390), "first door-close enters runtime");

        expect(update(playback, 490), "first door-close reaches its end");
        step = jail.tick(490, playback, ui);
        expect(step && jail.state() == 8 &&
            step->camera == BoardCameraView::CornerJail,
            "states 6-7 close, restart rolling and build the return route");
        expect(update(playback, 490), "return rolling paddy commands drain");

        step = jail.tick(560, playback, ui);
        expect(step && jail.state() == 9 &&
            step->camera == pickCameraFor3Squares(40),
            "return-route end switches to jail camera and door-open state");
        expect(update(playback, 560), "second door-open sequence starts at jail endpoint");

        expect(update(playback, 660), "second door-open reaches held end");
        step = jail.tick(660, playback, ui);
        expect(step && jail.state() == 10,
            "state 9 starts open-idle and token unload together");
        expect(update(playback, 660), "token-unload sequence enters runtime");
        expect(playback.runtime().info(threeD(0x0158), JailPlaybackPriority, false).has_value(),
            "CNK_knawjo token animation is active at priority 77");

        expect(update(playback, 760), "token-unload reaches held end");
        step = jail.tick(760, playback, ui);
        expect(step && jail.state() == 11 && !jail.playerInPaddywagon(),
            "state 11 restores ordinary token visibility and closes wagon");
        expect(update(playback, 760), "second door-close enters runtime");

        expect(update(playback, 860), "second door-close reaches held end");
        step = jail.tick(860, playback, ui);
        expect(step && jail.state() == 13 &&
            step->camera == pickCameraFor3Squares(40),
            "state 12 starts paddy pop-out and reasserts jail camera");
        expect(update(playback, 860), "paddy pop-out enters runtime");

        expect(update(playback, 960), "paddy pop-out reaches held end");
        step = jail.tick(960, playback, ui);
        expect(step && step->completed && !jail.active() &&
            ui.players[0].currentSquare == 40 && !jail.playerInPaddywagon(),
            "state 14 performs final cleanup and leaves player in jail");
        expect(update(playback, 960), "final paddy Stop command drains cleanly");
        expect(!playback.runtime().info(threeD(0x05CB), JailPlaybackPriority, false),
            "final cleanup removes CNK_mnwpo from runtime");
    }

    void testShortCircuitsAndValidation()
    {
        SyntheticSequenceResources fixture;
        engine::SequencePlayback playback(fixture.service.snapshot());
        rules::GameState ui{};
        ui.numberOfPlayers = 1;

        PieceJailPlayback disabled;
        ui.players[0].currentSquare = 16;
        expect(disabled.begin(request(16), 0, false).has_value(),
            "animations-disabled GoToJail request is accepted");
        auto step = disabled.tick(0, playback, ui);
        expect(step && step->completed && !disabled.active() &&
            ui.players[0].currentSquare == 40,
            "animations-disabled path jumps directly through source cleanup state 14");
        expect(playback.commands().pendingCount() == 0,
            "animations-disabled path does not invent paddy/token sequence commands");

        PieceJailPlayback alreadyAtJailCorner;
        ui.players[0].currentSquare = 10;
        expect(alreadyAtJailCorner.begin(request(10), 0, true).has_value(),
            "square-10 GoToJail request is accepted");
        step = alreadyAtJailCorner.tick(0, playback, ui);
        expect(step && step->completed && !alreadyAtJailCorner.active() &&
            ui.players[0].currentSquare == 40,
            "square-10 source skipAnimValue path completes without paddy travel");
        expect(playback.commands().pendingCount() == 0,
            "square-10 source skip path emits no synthetic animation work");

        PieceJailPlayback invalid;
        auto bad = request(16);
        bad.special = PieceMoveSpecial::LeaveJail;
        expect(!invalid.begin(bad, 0, true),
            "non-GoToJail special is rejected before state mutation");
        expect(!invalid.active(),
            "rejected special leaves jail playback inactive");
    }
}

int main()
{
    testFullGoToJailMachine();
    testShortCircuitsAndValidation();
    std::cout << (failures ? "Piece jail-playback tests FAILED\n" :
        "Piece jail-playback tests passed\n");
    return failures ? 1 : 0;
}
