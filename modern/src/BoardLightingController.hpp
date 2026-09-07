#pragma once

#include "World3DRenderer.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::boarddisplay
{
    inline constexpr std::uint64_t SpotlightIdleDelayTicks = 60U * 25U;
    inline constexpr std::uint64_t SpotlightFocusMoveTicks = 210U;
    inline constexpr float SpotlightFollowRatio = 0.3F;
    inline constexpr float SpotlightIdleIncrement = 3.0F;
    inline constexpr float SpotlightColourIncrement = 0.003F;

    struct BoardLightingInputs
    {
        bool game3DOn{true};
        bool board3DOn{};
        bool lightingOn{true};
        bool tokenAnimationActive{};
        std::uint64_t tick{};
        std::uint64_t numberOfTicks{};
        std::uint64_t lastBoardActivityTick{};
        std::array<float, 3> tokenPosition{};
        std::uint8_t playerColour{};
    };

    class BoardLightingController final
    {
    public:
        [[nodiscard]] std::expected<engine::World3DLighting, std::string>
        tick(const BoardLightingInputs& inputs);
        void reset() noexcept;
        [[nodiscard]] const engine::World3DLighting& current() const noexcept
        { return lighting_; }

    private:
        void beginSpotlight(std::uint64_t tick) noexcept;
        void updateSpotlightMotion(const BoardLightingInputs& inputs) noexcept;
        void updateSpotlightColour(const BoardLightingInputs& inputs) noexcept;
        [[nodiscard]] std::array<float, 3> interpolatedTarget(
            std::uint64_t tick) const noexcept;

        engine::World3DLighting lighting_{};
        bool spotlightActive_{};
        bool ascendingX_{true};
        bool ascendingZ_{true};
        bool fixedRatio_{};
        float fixedRatioValue_{};
        std::array<float, 3> startTarget_{240.0F, 300.0F, 240.0F};
        std::array<float, 3> endTarget_{240.0F, 300.0F, 240.0F};
        std::array<float, 3> currentTarget_{240.0F, 300.0F, 240.0F};
        std::array<float, 3> currentColour_{1.0F, 1.0F, 1.0F};
        std::uint64_t moveStartTick_{};
        std::uint64_t moveEndTick_{};
    };
}
