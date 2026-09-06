#include "SequenceBitmapRenderData.hpp"
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

    void testBitmapCollection()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        const auto snapshot = resources.service.snapshot();
        engine::SequencePlayback playback(snapshot);
        const auto face = data::packDataId(data::LegacyGroupId::Main, 0x0096);
        const auto bob = data::packDataId(data::LegacyGroupId::Main, 0x009C);

        expect(playback.startXY(face, 256, -35, 0).has_value(),
            "fixed die starts through generic 2D playback");
        expect(playback.startXY(bob, 257, -11, 0, true).has_value(),
            "bobbing die starts through generic drop-frames playback");
        expect(playback.update(0).has_value(),
            "2D runtime settles before bitmap collection");
        const auto collected = sequence::collectSequenceBitmapRenderData(
            playback.runtime(), snapshot);
        expect(collected.has_value() && collected->size() == 2,
            "collector publishes both active bitmap leaves transactionally");
        if (!collected || collected->size() != 2) return;

        expect((*collected)[0].metadata.width == 2 &&
            (*collected)[0].metadata.height == 2 &&
            (*collected)[0].metadata.bitsPerPixel == 24 &&
            (*collected)[0].bytes && (*collected)[0].bytes->size() == 70,
            "collector validates synthetic BMP24 metadata and retains payload lease");
        expect((*collected)[0].worldTransform.values[6] == -35.0F &&
            (*collected)[1].worldTransform.values[6] == -11.0F &&
            (*collected)[0].priority == 256 && (*collected)[1].priority == 257,
            "collector preserves runtime traversal order, priorities and Matrix2D positions");
    }
    void testTypeMismatchIsTransactional()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        const auto snapshot = resources.service.snapshot();
        engine::SequencePlayback playback(snapshot);
        const auto bad = data::packDataId(data::LegacyGroupId::Main, 0x009D);
        expect(playback.startXY(bad, 300, 5, 6).has_value() &&
            playback.commands().updateCycle(0).has_value(),
            "synthetic wrong-type bitmap sequence reaches runtime intent");
        const auto collected = sequence::collectSequenceBitmapRenderData(
            playback.runtime(), snapshot);
        expect(!collected && collected.error().code ==
            sequence::SequenceBitmapRenderDataErrorCode::TypeMismatch &&
            collected.error().contentsDataId ==
                data::packDataId(data::LegacyGroupId::Main, 1),
            "wrong DAT content type fails closed before publishing a partial bitmap list");
        expect(!playback.update(1) && playback.world2D().size() == 0 && playback.world().size() == 0,
            "integrated playback rejects wrong bitmap type and clears both render slots");
    }
}

int main()
{
    std::cout << "Monopoly sequence bitmap render-data tests\n"
              << "=========================================\n";
    try { testBitmapCollection(); testTypeMismatchIsTransactional(); }
    catch (const std::exception& e)
    {
        std::cerr << "[FAIL] unexpected exception: " << e.what() << '\n';
        ++failures;
    }
    std::cout << "Bitmap render-data failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
