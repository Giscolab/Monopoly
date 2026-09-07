#pragma once

#include "DataBanks.hpp"
#include "Display.hpp"
#include "PieceCamera.hpp"
#include "RuleTypes.hpp"
#include "SequencePlayback.hpp"
#include "SequenceTransforms.hpp"

#include <array>
#include <expected>
#include <optional>
#include <string>

namespace monopoly::boarddisplay
{
    inline constexpr std::uint16_t OwnershipPriorityBase = 12;
    inline constexpr data::DataTag Ownership2DNormalBaseTag = 0x035A;
    inline constexpr data::DataTag Ownership2DMortgagedBaseTag = 0x0000;
    inline constexpr data::DataTag Ownership3DMortgagedBaseTag = 0x00E9;
    inline constexpr data::DataTag Ownership3DNormalBaseTag = 0x00EF;
    inline constexpr std::uint32_t Ownership2DPerCamera = 168;
    inline constexpr std::uint32_t OwnershipColours = 6;
    inline constexpr float Ownership3DScale = 0.10F;

    enum class OwnershipHighlightKind : std::uint8_t
    {
        Bitmap2D,
        Mesh3D
    };

    struct OwnershipHighlight
    {
        data::DataId sequence{data::EmptyDataId};
        std::uint16_t priority{};
        OwnershipHighlightKind kind{OwnershipHighlightKind::Bitmap2D};
        sequence::Matrix3D transform3D{sequence::identity3D()};
    };

    using OwnershipHighlightPlan =
        std::array<std::optional<OwnershipHighlight>, rules::SquareCount>;

    [[nodiscard]] int ownershipPropertyIndex(int square) noexcept;

    [[nodiscard]] sequence::Matrix3D ownershipHighlight3DTransform(
        int square) noexcept;

    [[nodiscard]] std::expected<OwnershipHighlightPlan, std::string>
    planOwnershipHighlights(
        const rules::GameState& state,
        display::Screen2D view,
        bool board3DOn,
        pieces::BoardCameraView camera);

    class OwnershipHighlightPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const OwnershipHighlightPlan& plan,
            engine::SequencePlayback& playback);
        void reset() noexcept;

        [[nodiscard]] data::DataId current(std::size_t square) const noexcept
        { return square < current_.size() ? current_[square] : data::EmptyDataId; }

    private:
        std::array<data::DataId, rules::SquareCount> current_{};
    };
}
