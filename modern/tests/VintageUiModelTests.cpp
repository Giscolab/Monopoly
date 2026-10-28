#include "VintageUiModel.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void modelTest()
{
    using namespace monopoly;
    rules::GameState game;
    ibar::State bar;
    game.numberOfPlayers=2; game.currentPlayer=1;
    game.players[0].name=L"Alice"; game.players[0].cash=1200; game.players[0].token=3;
    game.players[1].name=L"Bob"; game.players[1].cash=-42; game.players[1].colour=4;
    bar.players[1].visible=true;
    game.options.hideCash=true;
    bar.actionButtonLayout=ibar::layout::ActionButtonLayout::BuyAuction;
    bar.activeActionButtonSlots=ibar::layout::actionButtonBit(ibar::layout::ActionButtonSlot::Main);
    bar.visiblePropertySlots=ibar::layout::propertyBit(1) | ibar::layout::propertyBit(3);
    game.squares[1].owner=1; game.squares[1].houses=2; game.squares[1].mortgaged=true;
    game.squares[3].owner=rules::NobodyPlayer;
    const auto first=vintage::makeModel(game,bar,display::Screen2D::Main);
    check(first.players.size()==2 && first.players[0].name==L"Alice" && first.players[0].cash==1200 && first.players[0].token==3, "player snapshot");
    check(first.players[1].cash==-42 && first.players[1].current && first.players[1].visible && first.players[1].colour==4, "current player snapshot");
    check(first.visibleProperties.size()==2 && first.visibleProperties[0].square==1 && first.visibleProperties[0].ownedByCurrentPlayer && first.visibleProperties[0].mortgaged && first.visibleProperties[0].houses==2, "visible properties snapshot");
    check(first.hideCash && first.actionLayout==bar.actionButtonLayout && first.activeActions==bar.activeActionButtonSlots && first.view==display::Screen2D::Main, "UI snapshot");
    game.currentPlayer=rules::NobodyPlayer; game.players[0].cash=17; bar.visiblePropertySlots=ibar::layout::propertyBit(3);
    const auto next=vintage::makeModel(game,bar,display::Screen2D::Main);
    check(next.players[0].cash==17 && first.players[0].cash==1200 && next.visibleProperties.size()==1 && !next.visibleProperties[0].ownedByCurrentPlayer, "fresh value snapshot, nobody is not an owner");
    game.numberOfPlayers=255; const auto limited=vintage::makeModel(game,bar,display::Screen2D::Main);
    check(limited.players.size()==rules::MaxPlayers, "player bounds");
}
}
int main() { try { modelTest(); std::cout << "[PASS] vintage model snapshot\n"; return 0; } catch (const std::exception& e) { std::cerr << "[FAIL] " << e.what() << '\n'; return 1; } }
