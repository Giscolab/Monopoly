#include "StatsBankPlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;

    void require(bool condition, const char* description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << description << std::endl;
        if (!condition) throw std::runtime_error(description);
    }

    void testRetailGeometry()
    {
        require(statsui::BankOverlayPriority == 55 &&
                statsui::BankIconPriority == 56 &&
                statsui::BankSignPriority == 60,
            "Bank overlays keep retail priorities 55/56/60");
        require(statsui::bankDeedRect(5) == statsui::Rect{583,100,619,142} &&
                statsui::bankDeedRect(12) == statsui::Rect{714,100,750,142} &&
                statsui::bankDeedRect(35) == statsui::Rect{648,49,684,91},
            "railroad and utility deeds keep exact retail Bank coordinates");
        require(statsui::bankDeedRect(1) == statsui::Rect{38,100,74,142} &&
                statsui::bankDeedSequence(1) ==
                    data::packDataId(data::LegacyGroupId::Patterns, 0x05E6),
            "Mediterranean uses first retail property slot and PAT deed");
        require(!statsui::bankDeedRect(0) &&
                statsui::bankDeedSequence(0) == data::EmptyDataId,
            "non-ownable board squares have no Bank deed");
    }

    void testHouseHotelPlayback()
    {
        rules::GameState game{};
        game.numberOfPlayers = 2;
        game.players[0].colour = 2;
        game.players[1].colour = 4;
        statsui::State state{};
        state.screen = statsui::Screen::Bank;
        state.activeSort = 0;
        state.bankPlayerHouses[0] = 3;
        state.bankPlayerHotels[0] = 1;
        state.bankPlayerHouses[1] = 1;

        SyntheticSequenceResources resources;
        engine::SequencePlayback sequence(resources.service.snapshot());
        statsui::BankPlayback playback;
        require(playback.sync(state, game, display::Screen2D::Portfolio, sequence) &&
                playback.objectCount() == 7 && sequence.update(0),
            "Bank Houses publishes two bars plus four houses and one hotel");

        const auto house = data::packDataId(data::LegacyGroupId::Main,
            statsui::BankHouseTag);
        const auto hotel = data::packDataId(data::LegacyGroupId::Main,
            statsui::BankHotelTag);
        require(sequence.runtime().matching(house, 56, false).size() == 4 &&
                sequence.runtime().matching(hotel, 56, false).size() == 1,
            "repeated house/hotel assets coexist at one retail priority");

        const auto houseRoots = sequence.runtime().matching(house, 56, false);
        bool colour2First = false, colour4First = false;
        for (const auto root : houseRoots)
        {
            const auto view = sequence.runtime().inspect(root);
            if (!view || !std::holds_alternative<sequence::Matrix2D>(view->localTransform)) continue;
            const auto& m = std::get<sequence::Matrix2D>(view->localTransform);
            colour2First = colour2First || (m.values[6] == 40.0F && m.values[7] == 345.0F);
            colour4First = colour4First || (m.values[6] == 40.0F && m.values[7] == 389.0F);
        }
        require(colour2First && colour4First,
            "house rows follow player colour rather than player slot");
        const auto hotels = sequence.runtime().matching(hotel, 56, false);
        const auto hotelView = hotels.empty() ? std::optional<sequence::SequenceNodeView>{} :
            sequence.runtime().inspect(hotels.front());
        require(hotelView &&
                std::get<sequence::Matrix2D>(hotelView->localTransform).values[6] == 418.0F &&
                std::get<sequence::Matrix2D>(hotelView->localTransform).values[7] == 346.0F,
            "hotel placement uses measured width and retail colour row");

        state.bankPlayerHouses[0] = 1;
        state.bankPlayerHotels[0] = 0;
        state.bankPlayerHouses[1] = 0;
        require(playback.sync(state, game, display::Screen2D::Portfolio, sequence) &&
                sequence.update(1) && playback.objectCount() == 3 &&
                sequence.runtime().matching(house, 56, false).size() == 1 &&
                sequence.runtime().matching(hotel, 56, false).empty(),
            "changed Bank inventory rebuilds duplicate sprites without stale roots");
    }

    void testPropertyPlayback()
    {
        rules::GameState game{};
        game.numberOfPlayers = 2;
        statsui::State state{};
        state.screen = statsui::Screen::Bank;
        state.activeSort = 1;
        for (int square = 0; square < static_cast<int>(rules::SquareCount); ++square)
            if (statsui::bankDeedSequence(square) != data::EmptyDataId)
                state.bankDeeds[static_cast<std::size_t>(square)] =
                    statsui::BankDeedState::Available;
        state.bankDeeds[1] = statsui::BankDeedState::Sold;
        state.bankDeeds[3] = statsui::BankDeedState::Mortgaged;

        SyntheticSequenceResources resources;
        engine::SequencePlayback sequence(resources.service.snapshot());
        statsui::BankPlayback playback;
        require(playback.sync(state, game, display::Screen2D::Portfolio, sequence) &&
                playback.objectCount() == 30 && sequence.update(0),
            "Bank Properties publishes 28 deeds and two ownership signs");

        const auto med = statsui::bankDeedSequence(1);
        const auto medRoots = sequence.runtime().matching(med, 55, false);
        const auto medView = medRoots.empty() ? std::optional<sequence::SequenceNodeView>{} :
            sequence.runtime().inspect(medRoots.front());
        require(medView &&
                std::get<sequence::Matrix2D>(medView->localTransform).values[6] == 45.0F &&
                std::get<sequence::Matrix2D>(medView->localTransform).values[7] == 326.0F,
            "Bank deed adds retail BankScreenBG offset 7,226");
        const auto sold = data::packDataId(data::LegacyGroupId::LanguageGraphics,
            statsui::BankSoldSignTag);
        const auto mort = data::packDataId(data::LegacyGroupId::LanguageGraphics,
            statsui::BankMortgageSignTag);
        const auto soldRoots = sequence.runtime().matching(sold, 60, false);
        const auto mortRoots = sequence.runtime().matching(mort, 60, false);
        const auto soldView = soldRoots.empty() ? std::optional<sequence::SequenceNodeView>{} :
            sequence.runtime().inspect(soldRoots.front());
        const auto mortView = mortRoots.empty() ? std::optional<sequence::SequenceNodeView>{} :
            sequence.runtime().inspect(mortRoots.front());
        require(soldView && mortView &&
                std::get<sequence::Matrix2D>(soldView->localTransform).values[6] == 62.0F &&
                std::get<sequence::Matrix2D>(soldView->localTransform).values[7] == 346.0F &&
                std::get<sequence::Matrix2D>(mortView->localTransform).values[6] == 62.0F &&
                std::get<sequence::Matrix2D>(mortView->localTransform).values[7] == 295.0F,
            "SOLD and MORTGAGED signs center from SOLD bitmap dimensions like retail");

        require(playback.sync(state, game, display::Screen2D::Main, sequence) &&
                sequence.update(1) && playback.objectCount() == 0 &&
                sequence.runtime().matching(med, 55, false).empty() &&
                sequence.runtime().matching(sold, 60, false).empty(),
            "leaving Portfolio removes all Bank overlay roots");
    }
}

int main()
{
    try
    {
        testRetailGeometry();
        testHouseHotelPlayback();
        testPropertyPlayback();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
