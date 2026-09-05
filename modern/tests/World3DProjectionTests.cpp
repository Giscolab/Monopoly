#include "World3DProjection.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace
{
    using namespace monopoly;
    int failures{};

    void expect(bool condition, std::string_view message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) ++failures;
    }

    bool near(float left, float right) noexcept
    { return std::fabs(left - right) < 0.0001F; }

    engine::World3DCamera testCamera()
    {
        engine::World3DCamera camera;
        camera.location = {0.0F, 0.0F, 0.0F};
        camera.fieldOfView = 1.5707963267948966F;
        camera.nearPlane = 1.0F;
        camera.farPlane = 100.0F;
        return camera;
    }

    void testMatricesAndScreenBounds()
    {
        const auto state = engine::makeWorld3DProjectionState(
            {0, 0, 800, 450}, testCamera());
        expect(state.has_value(), "valid camera and viewport build projection state");
        if (!state) return;
        expect(near(state->view.values[0], 1.0F) &&
            near(state->view.values[5], 1.0F) &&
            near(state->view.values[10], 1.0F),
            "origin +Z camera produces identity view orientation");
        expect(near(state->projection.values[0], 1.0F) &&
            near(state->projection.values[5], 1.0F) &&
            near(state->projection.values[10], 100.0F / 99.0F) &&
            near(state->projection.values[11], 1.0F),
            "projection matches PC3D Matrix::ProjectionMatrix convention");
        expect(near(state->viewportMatrix.values[0], 400.0F) &&
            near(state->viewportMatrix.values[5], -400.0F) &&
            near(state->viewportMatrix.values[12], 400.0F) &&
            near(state->viewportMatrix.values[13], 225.0F),
            "viewport matrix matches L_Rend3D SetViewport including Y flip");

        data::MeshBounds bounds{{-1.0F, -1.0F, 10.0F}, {1.0F, 1.0F, 12.0F}};
        const auto rect = engine::world3DMeshScreenRect(
            bounds, sequence::identity3D(), *state);
        expect(rect && *rect == engine::World3DRect{359, 184, 441, 266},
            "mesh screen rectangle uses projected box corners and one-pixel expansion");
    }

    void testNearPlaneClippingAndOffscreen()
    {
        const auto state = engine::makeWorld3DProjectionState(
            {0, 0, 800, 450}, testCamera()).value();
        data::MeshBounds crossing{{-1.0F, -1.0F, 0.5F}, {1.0F, 1.0F, 2.0F}};
        const auto clipped = engine::world3DMeshScreenRect(
            crossing, sequence::identity3D(), state);
        expect(clipped && *clipped == engine::World3DRect{0, 0, 800, 450},
            "near-plane edge intersections are included before viewport clipping");

        data::MeshBounds behind{{-1.0F, -1.0F, 0.1F}, {1.0F, 1.0F, 0.5F}};
        expect(!engine::world3DMeshScreenRect(
            behind, sequence::identity3D(), state),
            "mesh entirely behind near plane has no screen rectangle");

        const auto moved = sequence::translate3D(1000.0F, 0.0F, 0.0F);
        data::MeshBounds visible{{-1.0F, -1.0F, 10.0F}, {1.0F, 1.0F, 12.0F}};
        expect(!engine::world3DMeshScreenRect(visible, moved, state),
            "projected mesh fully outside viewport is rejected after clipping");
    }

    void testCameraTranslationAndValidation()
    {
        auto camera = testCamera();
        camera.location = {5.0F, 0.0F, 0.0F};
        const auto translated = engine::makeWorld3DProjectionState(
            {0, 0, 800, 450}, camera).value();
        data::MeshBounds bounds{{-1.0F, -1.0F, 10.0F}, {1.0F, 1.0F, 12.0F}};
        const auto world = sequence::translate3D(5.0F, 0.0F, 0.0F);
        const auto rect = engine::world3DMeshScreenRect(bounds, world, translated);
        expect(rect && *rect == engine::World3DRect{359, 184, 441, 266},
            "world then view composition preserves camera-relative placement");

        expect(!engine::makeWorld3DProjectionState({0, 0, 0, 450}, camera) &&
            !engine::makeWorld3DProjectionState({0, 0, 800, 450},
                engine::World3DCamera{camera.location, {0, 0, 0}, camera.up,
                    camera.fieldOfView, camera.nearPlane, camera.farPlane}),
            "invalid viewport and zero forward vector are rejected");
        auto badPlanes = camera;
        badPlanes.nearPlane = 100.0F;
        badPlanes.farPlane = 10.0F;
        expect(!engine::makeWorld3DProjectionState({0, 0, 800, 450}, badPlanes),
            "reversed clipping planes are rejected");
    }
}

int main()
{
    testMatricesAndScreenBounds();
    testNearPlaneClippingAndOffscreen();
    testCameraTranslationAndValidation();
    std::cout << "World3D projection failures: " << failures << '\n';
    return failures ? 1 : 0;
}
