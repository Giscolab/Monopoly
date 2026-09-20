#pragma once
#include "MousePointer.hpp"
#include "SequencePlayback.hpp"

namespace monopoly::mouse
{
    inline constexpr data::DataId PointerDataId =
        data::packDataId(data::LegacyGroupId::Main, 0x0323);
    // Main.cpp:349 mouse grouping priority; its sole child had priority 100.
    inline constexpr std::uint16_t PointerPriority = 0xFFFF;

    class Playback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state, engine::SequencePlayback& playback);
        // Reset only when the owning SequencePlayback is discarded.
        void reset() noexcept;
        [[nodiscard]] bool visible() const noexcept { return visible_; }
    private:
        bool visible_{};
        int x_{};
        int y_{};
        sequence::Matrix2D authoredTransform_{sequence::identity2D()};
    };
}
