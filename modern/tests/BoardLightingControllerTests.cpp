#include "BoardLightingController.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace
{
    int failures{};

    void expect(bool condition, std::string_view text)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << text << '\n';
        if (!condition) ++failures;
    }

    [[nodiscard]] bool near(float left, float right,
        float epsilon = 0.0001F) noexcept
    {
        return std::fabs(left - right) <= epsilon;
    }

    void testGlobalLights()
    {
        monopoly::boarddisplay::BoardLightingController controller;
        monopoly::boarddisplay::BoardLightingInputs inputs{};
        inputs.numberOfTicks = 1;
        inputs.tick = 1;
        const auto lit = controller.tick(inputs);
        expect(lit && near(lit->ambient[0], 0.53F) &&
            lit->boardReflection.enabled && lit->sun.enabled &&
            !lit->spotlight.enabled,
            "3D lighting uses ambient 0.53 and two directionals without a 3D board");
        expect(lit && near(lit->boardReflection.color[0], 0.2625F) &&
            near(lit->boardReflection.color[1], 0.301F) &&
            near(lit->boardReflection.color[2], 0.2695F) &&
            lit->boardReflection.direction == std::array<float, 3>{0.0F, 1.0F, 0.0F},
            "board reflection preserves retail colour and direction");
        expect(lit && lit->sun.color == std::array<float, 3>{0.28F, 0.28F, 0.28F} &&
            lit->sun.direction == std::array<float, 3>{0.0F, -1.0F, 0.0F},
            "sun preserves retail colour and direction");

        inputs.tick = 2;
        inputs.lightingOn = false;
        const auto low = controller.tick(inputs);
        expect(low && near(low->ambient[0], 0.84F) &&
            !low->boardReflection.enabled && !low->sun.enabled &&
            !low->spotlight.enabled,
            "lighting-off path raises ambient to retail 0.84 and disables lights");
    }

    void testSpotlightFocusAndAnimatedFollow()
    {
        monopoly::boarddisplay::BoardLightingController controller;
        monopoly::boarddisplay::BoardLightingInputs inputs{};
        inputs.board3DOn = true;
        inputs.tokenPosition = {100.0F, 0.0F, 100.0F};
        inputs.playerColour = 0;
        inputs.tick = 1;
        inputs.numberOfTicks = 1;
        const auto start = controller.tick(inputs);
        expect(start && start->spotlight.enabled &&
            start->spotlight.position == std::array<float, 3>{240.0F, 500.0F, 240.0F},
            "spotlight starts at retail 240,300,240 target plus 200 Y offset");
        expect(start && near(start->spotlight.color[0], 0.997F) &&
            near(start->spotlight.color[1], 0.997F) &&
            near(start->spotlight.color[2], 0.997F),
            "spotlight colour begins at white and moves by 0.003 per tick");

        inputs.tick = 106;
        inputs.numberOfTicks = 105;
        const auto half = controller.tick(inputs);
        expect(half && near(half->spotlight.position[0], 170.0F) &&
            near(half->spotlight.position[1], 350.0F) &&
            near(half->spotlight.position[2], 170.0F),
            "210-tick focus interpolation is exactly halfway at ratio 0.5");

        inputs.tick = 211;
        inputs.numberOfTicks = 105;
        const auto end = controller.tick(inputs);
        expect(end && end->spotlight.position ==
            std::array<float, 3>{100.0F, 200.0F, 100.0F},
            "focus interpolation reaches token target after exactly 210 ticks");

        inputs.tick = 212;
        inputs.numberOfTicks = 1;
        inputs.tokenAnimationActive = true;
        inputs.tokenPosition = {200.0F, 0.0F, 100.0F};
        const auto follow = controller.tick(inputs);
        expect(follow && near(follow->spotlight.position[0], 130.0F) &&
            near(follow->spotlight.position[1], 200.0F),
            "animated token tracking applies the historical fixed 0.3 ratio");
    }

    void testIdleAndColourContracts()
    {
        monopoly::boarddisplay::BoardLightingController controller;
        monopoly::boarddisplay::BoardLightingInputs inputs{};
        inputs.board3DOn = true;
        inputs.playerColour = 5;
        inputs.tokenPosition = {100.0F, 0.0F, 100.0F};
        inputs.tick = monopoly::boarddisplay::SpotlightIdleDelayTicks;
        inputs.numberOfTicks = 1;
        const auto boundary = controller.tick(inputs);
        expect(boundary && boundary->spotlight.position ==
            std::array<float, 3>{240.0F, 500.0F, 240.0F},
            "idle spotlight does not start at the exact 25-second boundary");

        inputs.tick = monopoly::boarddisplay::SpotlightIdleDelayTicks + 1U;
        const auto idle = controller.tick(inputs);
        expect(idle && idle->spotlight.position ==
            std::array<float, 3>{103.0F, 200.0F, 103.0F},
            "idle spotlight starts only after strict >25 seconds and moves 3 units per tick");

        inputs.tick = 1901;
        inputs.numberOfTicks = 400;
        inputs.lastBoardActivityTick = 1901;
        const auto orange = controller.tick(inputs);
        expect(orange && near(orange->spotlight.color[0], 0.75F) &&
            near(orange->spotlight.color[1], 0.75F * 128.0F / 255.0F) &&
            near(orange->spotlight.color[2], 0.0F),
            "orange spotlight converges to 75 percent of legacy player RGB");

        const auto beforeInvalid = controller.current();
        inputs.tick = 1902;
        inputs.numberOfTicks = 1;
        inputs.playerColour = 6;
        const auto invalid = controller.tick(inputs);
        expect(!invalid && controller.current().spotlight.position ==
            beforeInvalid.spotlight.position &&
            controller.current().spotlight.color == beforeInvalid.spotlight.color,
            "invalid player colour is rejected transactionally");
    }
}

int main()
{
    testGlobalLights();
    testSpotlightFocusAndAnimatedFollow();
    testIdleAndColourContracts();
    std::cout << "Board lighting controller failures: " << failures << '\n';
    return failures ? 1 : 0;
}
