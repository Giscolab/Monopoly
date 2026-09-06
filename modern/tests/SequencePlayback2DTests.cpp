#include "SequencePlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <string_view>

namespace
{
    int failures{};

    void expect(bool condition, std::string_view message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) ++failures;
    }

    void testFixedAndBobbingDice2D()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        const auto face = data::packDataId(data::LegacyGroupId::Main, 0x0096);
        const auto bob = data::packDataId(data::LegacyGroupId::Main, 0x009C);

        expect(playback.startXY(face, 256, -35, 0).has_value(),
            "StartXY queues fixed die at historical priority/offset");
        expect(playback.commands().pendingCount() == 2,
            "StartXY is one start plus one MoveXY command");
        expect(playback.update(0).has_value(),
            "fixed 2D die executes through SequencePlayback");
        auto items = playback.runtime().bitmapInstances();
        expect(items.size() == 1 && items[0].contentsDataId ==
            data::packDataId(data::LegacyGroupId::Main, 0x00A0) &&
            items[0].priority == 256 &&
            items[0].worldTransform.values[6] == -35.0F &&
            items[0].worldTransform.values[7] == 0.0F,
            "fixed die publishes resolved bitmap and historical Matrix2D");

        expect(playback.startXY(bob, 257, -11, 0, true).has_value(),
            "StartXYDrop queues second bobbing die with drop-frames enabled");
        expect(playback.setEndingAction(bob, 257, 3).has_value(),
            "bobbing die overrides ending action to LoopToBeginning");
        expect(playback.update(4).has_value(),
            "bobbing 2D die executes through the normal command cycle");
        items = playback.runtime().bitmapInstances();
        expect(items.size() == 2,
            "fixed and bobbing dice coexist as independent runtime leaves");
        const auto bobInfo = playback.runtime().info(bob, 257);
        expect(bobInfo && bobInfo->dimensionality == 2,
            "bobbing die remains a true 2D sequence after ending-action override");
        expect(playback.stop(face, 256).has_value() && playback.update(8).has_value(),
            "fixed die stops through the same playback API");
        items = playback.runtime().bitmapInstances();
        expect(items.size() == 1 && items[0].priority == 257 &&
            items[0].worldTransform.values[6] == -11.0F,
            "stopping one priority preserves the other 2D die instance");
    }
}

int main()
{
    std::cout << "Monopoly SequencePlayback 2D tests\n"
              << "==================================\n";
    try { testFixedAndBobbingDice2D(); }
    catch (const std::exception& e)
    {
        std::cerr << "[FAIL] unexpected exception: " << e.what() << '\n';
        ++failures;
    }
    std::cout << "SequencePlayback 2D failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
