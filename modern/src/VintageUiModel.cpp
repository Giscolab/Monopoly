#include "VintageUiModel.hpp"
#include <algorithm>

namespace monopoly::vintage
{
    Model makeModel(const rules::GameState& game, const ibar::State& bar,
        display::Screen2D view)
    {
        Model result;
        result.actionLayout = bar.actionButtonLayout;
        result.activeActions = bar.activeActionButtonSlots;
        result.view = view;
        result.hideCash = game.options.hideCash;
        const auto count = std::min<unsigned>(game.numberOfPlayers, rules::MaxPlayers);
        result.players.reserve(count);
        for (unsigned i=0; i<count; ++i)
        {
            const auto& player=game.players[i];
            result.players.push_back({static_cast<rules::PlayerNumber>(i), player.name,
                player.cash, player.token, player.colour, game.currentPlayer==i,
                bar.players[i].visible});
        }
        for (int square=0; square<static_cast<int>(rules::SquareCount); ++square)
        {
            const auto bit=ibar::layout::propertyBit(square);
            if (!bit || !(bar.visiblePropertySlots & bit)) continue;
            const auto& property=game.squares[static_cast<std::size_t>(square)];
            const bool currentValid=game.currentPlayer<count;
            result.visibleProperties.push_back({square, property.owner, property.houses,
                property.mortgaged, currentValid && property.owner==game.currentPlayer});
        }
        return result;
    }
}
