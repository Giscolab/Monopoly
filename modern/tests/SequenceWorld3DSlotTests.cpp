#include "SequenceWorld3DSlot.hpp"

#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace
{
    using namespace monopoly;
    int failures{};
    void expect(bool value, std::string_view text)
    {
        std::cout << (value ? "[PASS] " : "[FAIL] ") << text << '\n';
        if (!value) ++failures;
    }

    std::shared_ptr<const data::MeshRuntimeAsset> asset(data::DataId id)
    {
        return std::make_shared<const data::MeshRuntimeAsset>(
            data::MeshRuntimeAsset{id, {}, {}});
    }

    sequence::SequenceMeshRenderItem item(sequence::SequenceNodeId node,
        data::DataId id, float x = 0.0F)
    {
        auto matrix = sequence::identity3D();
        matrix.values[12] = x;
        return {node, id, 7, 3, matrix, asset(id)};
    }
    void testLifecycleAndStableIdentity()
    {
        engine::SequenceWorld3DSlot slot;
        expect(engine::SequenceWorld3DSlot::slot() == engine::RenderSlot::World3D,
            "sequence mesh scene is bound to historical render slot 1");
        auto first = item(10, data::packDataId(8, 1));
        auto second = item(20, data::packDataId(8, 2));
        auto sync = slot.sync({first, second});
        expect(sync && sync->started == 2 && sync->stopped == 0 && slot.size() == 2,
            "first publication creates one slot object per runtime node");
        expect(slot.order() == std::vector<sequence::SequenceNodeId>{10, 20},
            "slot preserves sequencer traversal order instead of map ordering semantics");

        const auto* before = slot.find(10);
        expect(before && before->contentsDataId == first.contentsDataId,
            "created slot object retains mesh content identity");
        first.worldTransform.values[12] = 5.0F;
        sync = slot.sync({first, second});
        expect(sync && sync->moved == 1 && sync->unchanged == 1 &&
            slot.find(10)->worldTransform.values[12] == 5.0F,
            "SequenceMoved equivalent updates matrix without recreating the node");
    }
    void testRemovalAndTransactionalDuplicateFailure()
    {
        engine::SequenceWorld3DSlot slot;
        auto first = item(10, data::packDataId(8, 1));
        auto second = item(20, data::packDataId(8, 2));
        expect(slot.sync({first, second}).has_value(), "initial slot state builds");
        auto sync = slot.sync({second});
        expect(sync && sync->stopped == 1 && sync->started == 0 &&
            !slot.find(10) && slot.find(20),
            "SequenceShutDown equivalent removes disappeared runtime nodes");

        auto duplicate = second;
        duplicate.worldTransform.values[12] = 99.0F;
        const auto failed = slot.sync({second, duplicate});
        expect(!failed && failed.error().code ==
            engine::SequenceWorld3DSlotErrorCode::DuplicateNode,
            "duplicate runtime identity is rejected before publication");
        expect(slot.size() == 1 && slot.find(20) &&
            slot.find(20)->worldTransform.values[12] == second.worldTransform.values[12],
            "failed slot sync leaves the previous scene unchanged");
        slot.clear();
        expect(slot.size() == 0 && slot.order().empty(),
            "slot shutdown clears all CPU scene ownership");
    }
}

int main()
{
    testLifecycleAndStableIdentity();
    testRemovalAndTransactionalDuplicateFailure();
    std::cout << "Sequence World3D slot failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
