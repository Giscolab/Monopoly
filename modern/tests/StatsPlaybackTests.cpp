#include "StatsPlayback.hpp"
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
}

int main()
{
    using namespace monopoly;

    require(statsui::StatusBarPriority == 54 &&
            statsui::StatusBarOverlayPriority == 154 &&
            statsui::CategoryIdlePriorities == std::array<std::uint16_t,3>{56,58,60} &&
            statsui::CategoryPressPriorities == std::array<std::uint16_t,3>{57,59,61} &&
            statsui::SortIdlePriorities == std::array<std::uint16_t,4>{62,60,58,56} &&
            statsui::SortPressPriorities == std::array<std::uint16_t,4>{63,61,59,57},
        "UDStats playback keeps exact retail bar/button priorities");
    require(statsui::StatusBackgroundTag == 0x0089 &&
            statsui::DeedBackgroundTag == 0x00CD &&
            statsui::BankBackgroundTags == std::array<data::DataTag,4>{0x000C,0x000E,0x000D,0x000B},
        "UDStats playback uses exact Portfolio, Deed and Bank background tags");

    SyntheticSequenceResources resources;
    engine::SequencePlayback sequences(resources.service.snapshot());
    statsui::Playback playback;
    statsui::State state{};

    require(playback.sync(state, display::Screen2D::Main, sequences) &&
            sequences.commands().pendingCount() == 0 && !playback.visible(),
        "UDStats playback stays absent outside Portfolio");

    require(playback.sync(state, display::Screen2D::Portfolio, sequences) &&
            sequences.commands().pendingCount() == 14 && playback.visible(),
        "opening Portfolio publishes background, bars and seven stable status buttons");
    require(sequences.update(0).has_value(),
        "Portfolio opening transition executes");
    require(sequences.runtime().matching(
                statsui::languageSequence(statsui::StatusBackgroundTag), 10).size() == 1 &&
            sequences.runtime().matching(
                statsui::languageSequence(0x019F), 57).size() == 1 &&
            sequences.runtime().matching(
                statsui::languageSequence(0x01A5), 63).size() == 1,
        "Player category and Turn sort open pressed over the retail status background");

    state.screen = statsui::Screen::Deed;
    state.activeSort = 0;
    require(playback.sync(state, display::Screen2D::Portfolio, sequences).has_value() &&
            sequences.update(1).has_value(),
        "Player to Deed transition executes atomically");
    const auto deedRoots = sequences.runtime().matching(
        statsui::mainSequence(statsui::DeedBackgroundTag),
        statsui::DeedBackgroundPriority);
    const auto* deed = deedRoots.empty() ? nullptr : sequences.world2D().find(deedRoots.front());
    require(deed && deed->worldTransform.values[6] == 5.0F &&
            deed->worldTransform.values[7] == 229.0F,
        "Deed background preserves retail StartXY(5,229)");
    require(playback.categoryVisual(0) == statsui::ButtonVisual::Return &&
            playback.categoryVisual(1) == statsui::ButtonVisual::Press &&
            sequences.runtime().matching(statsui::languageSequence(0x01A0),56).size() == 1 &&
            sequences.runtime().matching(statsui::languageSequence(0x0180),59).size() == 1,
        "category handoff preserves retail Return then Press animation states");
    require(sequences.runtime().matching(statsui::languageSequence(0x01A2),63).size() == 1,
        "Deed Purchase Value sort starts pressed at retail priority");

    state.activeSort = 3;
    require(playback.sync(state, display::Screen2D::Portfolio, sequences).has_value() &&
            sequences.update(2).has_value() &&
            playback.sortVisual(0) == statsui::ButtonVisual::Return &&
            playback.sortVisual(3) == statsui::ButtonVisual::Press,
        "changing Deed sort keeps previous button on Return and new button Pressed");
    require(sequences.runtime().matching(statsui::languageSequence(0x01A3),62).size() == 1 &&
            sequences.runtime().matching(statsui::languageSequence(0x0183),57).size() == 1,
        "Deed Return/Press sequences use the exact idle/pressed priorities");

    state.screen = statsui::Screen::Bank;
    state.activeSort = 0;
    const auto bankSync = playback.sync(state, display::Screen2D::Portfolio, sequences);
    if (!bankSync) std::cout << "Bank sync error: " << bankSync.error() << std::endl;
    require(bankSync.has_value() && sequences.update(3).has_value(),
        "Deed to Bank transition executes");
    require(sequences.runtime().matching(
                statsui::mainSequence(statsui::BankBackgroundTags[0]),50).size() == 1 &&
            sequences.runtime().matching(statsui::languageSequence(0x00FF),63).size() == 1,
        "Bank Houses/Hotels publishes its retail background and pressed sort");

    state.activeSort = 2;
    require(playback.sync(state, display::Screen2D::Portfolio, sequences).has_value() &&
            sequences.update(4).has_value() &&
            sequences.runtime().matching(
                statsui::mainSequence(statsui::BankBackgroundTags[2]),50).size() == 1,
        "Bank Liabilities background remains visually faithful even while data is deferred");

    require(playback.sync(state, display::Screen2D::Main, sequences).has_value() &&
            sequences.update(5).has_value() && !playback.visible() &&
            sequences.runtime().matching(
                statsui::languageSequence(statsui::StatusBackgroundTag),10).empty(),
        "leaving Portfolio removes all autonomous UDStats roots");

    return 0;
}