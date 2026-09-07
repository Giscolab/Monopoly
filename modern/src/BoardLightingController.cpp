#include "BoardLightingController.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace monopoly::boarddisplay
{
    namespace
    {
        constexpr float Pi = 3.14159265358979323846F;
        constexpr std::array<std::array<std::uint8_t, 3>, 6> PlayerColours{{
            {{255U,   0U,   0U}},
            {{  0U,   0U, 255U}},
            {{ 60U, 150U,  60U}},
            {{255U, 255U,   0U}},
            {{255U,   0U, 255U}},
            {{255U, 128U,   0U}}
        }};

        [[nodiscard]] float accelerateInOut(float ratio) noexcept
        {
            ratio = std::clamp(ratio, 0.0F, 1.0F);
            if (ratio < 0.5F)
                return 0.5F - 0.5F * std::cos(ratio * Pi);
            return 0.5F + 0.5F * std::sin((ratio - 0.5F) * Pi);
        }

        [[nodiscard]] std::array<float, 3> lerp3(
            const std::array<float, 3>& start,
            const std::array<float, 3>& end, float ratio) noexcept
        {
            return {
                start[0] + (end[0] - start[0]) * ratio,
                start[1] + (end[1] - start[1]) * ratio,
                start[2] + (end[2] - start[2]) * ratio};
        }

        void approach(float& value, float target) noexcept
        {
            if (std::fabs(value - target) <= SpotlightColourIncrement)
                value = target;
            else if (value > target)
                value -= SpotlightColourIncrement;
            else
                value += SpotlightColourIncrement;
        }

        [[nodiscard]] std::array<float, 3> targetColour(
            std::uint8_t colour) noexcept
        {
            constexpr float Magnifier = 0.75F / 255.0F;
            const auto& rgb = PlayerColours[colour];
            return {rgb[0] * Magnifier, rgb[1] * Magnifier,
                rgb[2] * Magnifier};
        }
    }

    void BoardLightingController::reset() noexcept
    {
        lighting_ = {};
        spotlightActive_ = false;
        ascendingX_ = true;
        ascendingZ_ = true;
        fixedRatio_ = false;
        fixedRatioValue_ = 0.0F;
        startTarget_ = {240.0F, 300.0F, 240.0F};
        endTarget_ = startTarget_;
        currentTarget_ = startTarget_;
        currentColour_ = {1.0F, 1.0F, 1.0F};
        moveStartTick_ = 0;
        moveEndTick_ = 0;
    }

    void BoardLightingController::beginSpotlight(std::uint64_t tick) noexcept
    {
        spotlightActive_ = true;
        fixedRatio_ = false;
        fixedRatioValue_ = 0.0F;
        startTarget_ = {240.0F, 300.0F, 240.0F};
        endTarget_ = startTarget_;
        currentTarget_ = startTarget_;
        moveStartTick_ = tick;
        moveEndTick_ = tick;
    }

    std::array<float, 3> BoardLightingController::interpolatedTarget(
        std::uint64_t tick) const noexcept
    {
        float ratio = fixedRatioValue_;
        if (!fixedRatio_)
        {
            if (moveEndTick_ == moveStartTick_)
                ratio = 1.0F;
            else
                ratio = static_cast<float>(tick - moveStartTick_) /
                    static_cast<float>(moveEndTick_ - moveStartTick_);
            ratio = accelerateInOut(ratio);
        }
        else
            ratio = std::clamp(ratio, 0.0F, 1.0F);
        return lerp3(startTarget_, endTarget_, ratio);
    }

    void BoardLightingController::updateSpotlightMotion(
        const BoardLightingInputs& inputs) noexcept
    {
        for (std::uint64_t t = 0; t < inputs.numberOfTicks; ++t)
        {
            const bool idle =
                inputs.tick > inputs.lastBoardActivityTick + SpotlightIdleDelayTicks &&
                inputs.board3DOn;
            if (idle)
            {
                fixedRatio_ = false;
                moveStartTick_ = inputs.tick;
                moveEndTick_ = inputs.tick;
                if (ascendingX_)
                {
                    if (endTarget_[0] < 420.0F)
                        endTarget_[0] += SpotlightIdleIncrement;
                    else
                        ascendingX_ = false;
                }
                else if (endTarget_[0] > 0.0F)
                    endTarget_[0] -= SpotlightIdleIncrement;
                else
                    ascendingX_ = true;

                if (ascendingZ_)
                {
                    if (endTarget_[2] < 420.0F)
                        endTarget_[2] += SpotlightIdleIncrement;
                    else
                        ascendingZ_ = false;
                }
                else if (endTarget_[2] > 0.0F)
                    endTarget_[2] -= SpotlightIdleIncrement;
                else
                    ascendingZ_ = true;
            }
            else if (!inputs.tokenAnimationActive)
            {
                if (endTarget_ != inputs.tokenPosition)
                {
                    startTarget_ = currentTarget_;
                    endTarget_ = inputs.tokenPosition;
                    fixedRatio_ = false;
                    moveStartTick_ = inputs.tick;
                    moveEndTick_ = inputs.tick + SpotlightFocusMoveTicks;
                }
            }
            else
            {
                startTarget_ = currentTarget_;
                endTarget_ = inputs.tokenPosition;
                fixedRatio_ = true;
                fixedRatioValue_ = SpotlightFollowRatio;
            }
        }
        currentTarget_ = interpolatedTarget(inputs.tick);
    }

    void BoardLightingController::updateSpotlightColour(
        const BoardLightingInputs& inputs) noexcept
    {
        const auto target = targetColour(inputs.playerColour);
        for (std::uint64_t t = 0; t < inputs.numberOfTicks; ++t)
            for (std::size_t channel = 0; channel < currentColour_.size(); ++channel)
                approach(currentColour_[channel], target[channel]);
    }

    std::expected<engine::World3DLighting, std::string>
    BoardLightingController::tick(const BoardLightingInputs& inputs)
    {
        if (inputs.numberOfTicks == 0)
            return lighting_;

        const bool commonLightsOn = inputs.game3DOn && inputs.lightingOn;
        const bool spotlightOn = commonLightsOn && inputs.board3DOn;
        if (spotlightOn && inputs.playerColour >= PlayerColours.size())
            return std::unexpected(
                "board spotlight player colour is outside legacy 0..5 range");

        const float ambient = commonLightsOn ? 0.53F : 0.84F;
        lighting_.ambient = {ambient, ambient, ambient};

        lighting_.boardReflection.enabled = commonLightsOn;
        lighting_.boardReflection.color = {0.75F * 0.35F,
            0.86F * 0.35F, 0.77F * 0.35F};
        lighting_.boardReflection.direction = {0.0F, 1.0F, 0.0F};
        lighting_.sun.enabled = commonLightsOn;
        lighting_.sun.color = {0.28F, 0.28F, 0.28F};
        lighting_.sun.direction = {0.0F, -1.0F, 0.0F};

        lighting_.spotlight.enabled = spotlightOn;
        if (!spotlightOn)
        {
            spotlightActive_ = false;
            return lighting_;
        }
        if (!spotlightActive_)
            beginSpotlight(inputs.tick);

        updateSpotlightMotion(inputs);
        updateSpotlightColour(inputs);
        lighting_.spotlight.color = currentColour_;
        lighting_.spotlight.position = {currentTarget_[0],
            currentTarget_[1] + 200.0F, currentTarget_[2]};
        lighting_.spotlight.direction = {0.0F, -1.0F, 0.0F};
        lighting_.spotlight.attenuation = {0.0F, 1.0F, 0.0F};
        lighting_.spotlight.range = 300.0F;
        lighting_.spotlight.falloff = 4.0F;
        lighting_.spotlight.theta = Pi / 7.0F;
        lighting_.spotlight.phi = Pi / 2.0F;
        return lighting_;
    }
}
