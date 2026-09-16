#include "StatsCalculatorPlayback.hpp"
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

    [[nodiscard]] data::DataId mainId(data::DataTag tag)
    {
        return data::packDataId(data::LegacyGroupId::Main, tag);
    }

    [[nodiscard]] data::DataId languageId(data::DataTag tag)
    {
        return data::packDataId(data::LegacyGroupId::LanguageGraphics, tag);
    }

    void testRetailCatalog()
    {
        require(statsui::CalculatorBackgroundPriority == 50 &&
                statsui::CalculatorButtonPriority == 100,
            "calculator preserves retail priorities 50 and 100");        require(statsui::CalculatorBackgroundTag == 0x006A &&
                statsui::CalculatorFunctionIdleBaseTag == 0x006D &&
                statsui::CalculatorNumberIdleBaseTag == 0x007D &&
                statsui::CalculatorTextBoxTag == 0x0091,
            "calculator DAT_MAIN tags match retail catalog");
        require(statsui::CalculatorEnterIdleTag == 0x01FE &&
                statsui::CalculatorEnterIdleAlternateTag == 0x0388,
            "calculator Enter supports both legacy language-graphics catalogs");
    }

    void testPortfolioPlayback()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback sequence(resources.service.snapshot());
        statsui::CalculatorPlayback playback;

        require(playback.sync(display::Screen2D::Portfolio, sequence).has_value(),
            "Portfolio queues the autonomous calculator layer");
        require(playback.visible() && sequence.commands().pendingCount() == 21,
            "calculator queues exactly 21 retail sequences");
        require(sequence.update(0).has_value(),
            "calculator sequences execute through SequencePlayback");

        require(sequence.runtime().matching(
                    mainId(statsui::CalculatorBackgroundTag), 50, false).size() == 1 &&
                sequence.runtime().matching(
                    mainId(statsui::CalculatorTextBoxTag), 50, false).size() == 1,
            "calculator background and textbox render at priority 50");
        for (std::uint32_t index = 0; index < 10; ++index)
            require(sequence.runtime().matching(mainId(static_cast<data::DataTag>(
                        statsui::CalculatorNumberIdleBaseTag + index)),
                    100, false).size() == 1,
                "each numeric calculator button has one idle root");
        for (std::uint32_t index = 0; index < 8; ++index)
            require(sequence.runtime().matching(mainId(static_cast<data::DataTag>(
                        statsui::CalculatorFunctionIdleBaseTag + index)),
                    100, false).size() == 1,
                "each function calculator button has one idle root");
        require(sequence.runtime().matching(
                    languageId(statsui::CalculatorEnterIdleTag), 100, false).size() == 1,
            "Enter uses the active language-graphics sequence at priority 100");

        require(playback.sync(display::Screen2D::Portfolio, sequence).has_value() &&
                sequence.commands().pendingCount() == 0,
            "unchanged Portfolio calculator emits no redundant commands");

        require(playback.sync(display::Screen2D::Main, sequence).has_value() &&
                sequence.commands().pendingCount() == 21 && sequence.update(1).has_value(),
            "leaving Portfolio queues and executes calculator teardown");
        require(!playback.visible() &&
                sequence.runtime().matching(
                    mainId(statsui::CalculatorBackgroundTag), 50, false).empty(),
            "calculator layer is absent outside Portfolio");
    }
}

int main()
{
    try
    {
        testRetailCatalog();
        testPortfolioPlayback();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
