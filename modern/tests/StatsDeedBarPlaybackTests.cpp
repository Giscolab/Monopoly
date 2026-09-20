#include "StatsDeedBarPlayback.hpp"
#include "SyntheticTextResources.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <variant>

namespace
{
    using namespace monopoly;

    void require(bool condition, const char* description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << description << std::endl;
        if (!condition) throw std::runtime_error(description);
    }

    statsui::State deedState()
    {
        statsui::State state{};
        state.screen = statsui::Screen::Deed;
        state.activeSort = 1;
        for (std::size_t i = 0; i < rules::SquareCount; ++i)
            state.deedOrder[i] = static_cast<std::uint8_t>(i);
        return state;
    }
    void testOwnerBars()
    {
        rules::GameState game{};
        game.numberOfPlayers = 2;
        game.players[0].colour = 2;
        game.players[1].colour = 4;
        game.squares[1].owner = 0;
        game.squares[3].owner = 1;
        auto state = deedState();

        SyntheticTextResources resources({}, {{0x00D0, {data::LegacyDataType::Chunky, SyntheticSequenceResources::words({0x03000014, 0, 0x04000000, 2, 0x000000A0})}}});
        engine::SequencePlayback sequence(resources.service.snapshot());
        statsui::DeedBarPlayback playback;
        require(playback.sync(state, game, {},
                    display::Screen2D::Portfolio, sequence) &&
                playback.objectCount() == 2 && sequence.update(0),
            "Owner sort publishes one static colour bar per owned deed");

        const auto player0Bar = data::packDataId(
            data::LegacyGroupId::Main,
            static_cast<data::DataTag>(statsui::DeedOwnerBarBaseTag + 2));
        const auto roots = sequence.runtime().matching(
            player0Bar, statsui::DeedOwnerBarPriority, false);
        const auto view = roots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(roots.front());
        require(view &&
                std::get<sequence::Matrix2D>(view->localTransform).values[6] == 64.0F &&
                std::get<sequence::Matrix2D>(view->localTransform).values[7] == 247.0F,
            "Owner bar keeps retail deed offset (42,13)");

        state.activeSort = 0;
        require(playback.sync(state, game, {},
                    display::Screen2D::Portfolio, sequence) && sequence.update(1) &&
                playback.objectCount() == 28,
            "price sort publishes one value background per property");
        require(sequence.runtime().matching(player0Bar, statsui::DeedOwnerBarPriority, false).empty(),
            "price sort removes the old owner-colour bars");
        state.activeSort = 2;
        require(playback.sync(state, game, {}, display::Screen2D::Portfolio, sequence) && sequence.update(2) && playback.objectCount() == 28,
            "rent sort keeps all 28 property value backgrounds");
        state.activeSort = 3;
        require(playback.sync(state, game, {}, display::Screen2D::Portfolio, sequence) && sequence.update(3) && playback.objectCount() == 0,
            "earnings sort omits all zero-earning properties");
        state.deedMetric[1] = 123;
        require(playback.sync(state, game, {}, display::Screen2D::Portfolio, sequence) && sequence.update(4) && playback.objectCount() == 1,
            "earnings sort publishes the nonzero property value background");
    }
}

int main()
{
    try
    {
        testOwnerBars();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
