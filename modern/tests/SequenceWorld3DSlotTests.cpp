#include "SequenceWorld3DSlot.hpp"

#include <cmath>
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
    bool near(float a, float b) { return std::fabs(a - b) < 0.0001F; }

    std::shared_ptr<const data::MeshRuntimeAsset> asset(data::DataId id)
    {
        auto render = std::make_shared<data::MeshRenderData>();
        render->bounds = {{-1.0F, -1.0F, 10.0F}, {1.0F, 1.0F, 12.0F}};
        return std::make_shared<const data::MeshRuntimeAsset>(
            data::MeshRuntimeAsset{id, {}, std::move(render)});
    }

    sequence::SequenceMeshRenderItem item(sequence::SequenceNodeId node,
        data::DataId id, float x = 0.0F)
    {
        auto matrix = sequence::identity3D();
        matrix.values[12] = x;
        return {node, id, 7, 3, matrix, asset(id)};
    }

    void testCameraCommandResolution()
    {
        sequence::SequenceRuntime runtime;
        sequence::SetCameraCommand direct{1, 0, {10.0F, 20.0F, 30.0F},
            {0.0F, 0.0F, 2.0F}, {0.0F, 3.0F, 0.0F},
            0.75F, 2.0F, 1024.0F};
        const auto directCamera = engine::resolveWorld3DCamera(direct, runtime);
        expect(directCamera && directCamera->location == direct.position &&
            directCamera->forward == direct.forwards && directCamera->up == direct.up &&
            directCamera->fieldOfView == 0.75F && directCamera->nearPlane == 2.0F &&
            directCamera->farPlane == 1024.0F,
            "direct SetCamera preserves user vectors and projection values exactly");

        auto labelled = direct;
        labelled.cameraNumber = 42;
        expect(!engine::resolveWorld3DCamera(labelled, runtime),
            "missing labelled camera requests no render-slot update instead of inventing a fallback");

        constexpr float halfPi = 1.57079632679489661923F;
        sequence::SequenceCamera3DView sequenceCamera;
        sequenceCamera.worldTransform = sequence::moveRySTxzTransform(
            halfPi, 2.0F, 10.0F, 20.0F);
        sequenceCamera.fieldOfView = 0.5F;
        sequenceCamera.nearPlane = 3.0F;
        sequenceCamera.farPlane = 900.0F;
        const auto converted = engine::world3DCameraFromSequence(sequenceCamera);
        expect(near(converted.location[0], 10.0F) && near(converted.location[1], 0.0F) &&
            near(converted.location[2], 20.0F) && near(converted.forward[0], 1.0F) &&
            near(converted.forward[1], 0.0F) && near(converted.forward[2], 0.0F) &&
            near(converted.up[0], 0.0F) && near(converted.up[1], 1.0F) &&
            near(converted.up[2], 0.0F) && converted.fieldOfView == 0.5F &&
            converted.nearPlane == 3.0F && converted.farPlane == 900.0F,
            "sequenced camera transforms origin/+Z/+Y and normalizes scale exactly like L_Rend3D");
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

    void testProjectionVisibilityLifecycle()
    {
        engine::SequenceWorld3DSlot slot;
        auto first = item(10, data::packDataId(8, 1));
        auto second = item(20, data::packDataId(8, 2));
        expect(slot.sync({first, second}).has_value(),
            "objects can exist before a 3D view is configured");
        engine::World3DCamera camera;
        camera.location = {0.0F, 0.0F, 0.0F};
        camera.fieldOfView = 1.5707963267948966F;
        camera.nearPlane = 1.0F;
        camera.farPlane = 100.0F;
        const auto configured = slot.configureView({0, 0, 800, 450}, camera);
        expect(configured && *configured == 2 && slot.view().has_value(),
            "camera or viewport change recomputes every existing 3D object");
        expect(slot.find(10)->screenBounds == engine::World3DRect{359, 184, 441, 266} &&
            slot.visibleOrder() == std::vector<sequence::SequenceNodeId>{10, 20},
            "slot stores source-style clipped screen bounds in traversal order");

        first.worldTransform.values[12] = 1000.0F;
        const auto moved = slot.sync({first, second});
        expect(moved && moved->moved == 1 && !slot.find(10)->screenBounds &&
            slot.visibleOrder() == std::vector<sequence::SequenceNodeId>{20},
            "SequenceMoved refreshes visibility without touching unchanged objects");

        const auto oldView = slot.view()->viewport;
        const auto invalid = slot.configureView({0, 0, 0, 450}, camera);
        expect(!invalid && slot.view() && slot.view()->viewport == oldView &&
            slot.find(20)->screenBounds,
            "invalid view configuration is transactional and preserves published bounds");
        slot.clearView();
        expect(!slot.view() && slot.visibleOrder().empty() && slot.size() == 2,
            "view shutdown removes projected state without destroying sequence ownership");
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
    std::cout << std::unitbuf;
    testCameraCommandResolution();
    testLifecycleAndStableIdentity();
    testProjectionVisibilityLifecycle();
    testRemovalAndTransactionalDuplicateFailure();
    std::cout << "Sequence World3D slot failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
