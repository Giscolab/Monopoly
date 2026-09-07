#include "BoardOwnershipHighlight.hpp"

#include "BoardGeometry.hpp"
#include "BoardRules.hpp"
#include "LegacyBitmap.hpp"

#include <cmath>
#include <numbers>
#include <vector>

namespace monopoly::boarddisplay
{
    int ownershipPropertyIndex(int square) noexcept
    {
        if (square < 0 || square >= static_cast<int>(rules::SquareCount))
            return -1;
        const auto type = static_cast<rules::board::SquareType>(square);
        if (!rules::board::isOwnable(type)) return -1;
        int result = 0;
        for (int test = 0; test < square; ++test)
            if (rules::board::isOwnable(
                    static_cast<rules::board::SquareType>(test)))
                ++result;
        return result;
    }

    sequence::Matrix3D ownershipHighlight3DTransform(int square) noexcept
    {
        constexpr int JustVisiting =
            static_cast<int>(rules::board::SquareType::JustVisiting);
        constexpr int FreeParking =
            static_cast<int>(rules::board::SquareType::FreeParking);
        constexpr int GoToJail =
            static_cast<int>(rules::board::SquareType::GoToJail);
        constexpr int InJail =
            static_cast<int>(rules::board::SquareType::InJail);
        int tile = square == InJail ? JustVisiting : square;
        if (tile < 0 || tile >= static_cast<int>(rules::SquareCount))
            return sequence::identity3D();

        const auto& point = boardgeometry::locations3D()[
            static_cast<std::size_t>(tile)];
        constexpr float horizontal = -19.0F;
        constexpr float vertical = -8.0F;
        float x{};
        float z{};
        float yaw{};
        if (tile < JustVisiting)
        {
            x = point.x + vertical; z = point.z + horizontal; yaw = 0.0F;
        }
        else if (tile < FreeParking)
        {
            x = point.x + horizontal; z = point.z - vertical;
            yaw = std::numbers::pi_v<float> / 2.0F;
        }
        else if (tile < GoToJail)
        {
            x = point.x - vertical; z = point.z - horizontal;
            yaw = std::numbers::pi_v<float>;
        }
        else
        {
            x = point.x - horizontal; z = point.z + vertical;
            yaw = -std::numbers::pi_v<float> / 2.0F;
        }
        return sequence::moveRySTxzTransform(
            yaw, Ownership3DScale, x, z);
    }

    std::expected<OwnershipHighlightPlan, std::string> planOwnershipHighlights(
        const rules::GameState& state, display::Screen2D view,
        bool board3DOn, pieces::BoardCameraView camera)
    {
        OwnershipHighlightPlan result{};
        if (!display::isBoardVisible(view)) return result;

        const bool use3D = board3DOn || view == display::Screen2D::Portfolio ||
            view == display::Screen2D::Trade;
        const auto cameraIndex = static_cast<std::uint32_t>(camera);
        if (!use3D && cameraIndex >=
                static_cast<std::uint32_t>(pieces::BoardCameraView::Count))
            return std::unexpected("ownership highlight camera is outside legacy 0..38 range");

        for (std::size_t square = 0; square < rules::SquareCount; ++square)
        {
            const auto owner = state.squares[square].owner;
            if (owner >= rules::MaxPlayers) continue;
            const auto colour = state.players[owner].colour;
            if (colour >= OwnershipColours)
                return std::unexpected("ownership highlight player colour is outside legacy 0..5 range");
            const auto priority = static_cast<std::uint16_t>(
                OwnershipPriorityBase + square);
            const bool mortgaged = state.squares[square].mortgaged;

            if (use3D)
            {
                const auto base = mortgaged
                    ? Ownership3DMortgagedBaseTag : Ownership3DNormalBaseTag;
                result[square] = OwnershipHighlight{
                    data::packDataId(data::LegacyGroupId::ThreeD,
                        static_cast<data::DataTag>(base + colour)),
                    priority, OwnershipHighlightKind::Mesh3D,
                    ownershipHighlight3DTransform(static_cast<int>(square))};
                continue;
            }

            const int property = ownershipPropertyIndex(static_cast<int>(square));
            if (property < 0)
                return std::unexpected(
                    "2D ownership highlight encountered an owned non-ownable square");
            const std::uint32_t offset = cameraIndex * Ownership2DPerCamera +
                static_cast<std::uint32_t>(property) * OwnershipColours + colour;
            const auto base = mortgaged
                ? Ownership2DMortgagedBaseTag : Ownership2DNormalBaseTag;
            const auto tag = static_cast<std::uint32_t>(base) + offset;
            if (tag > 0xFFFFU)
                return std::unexpected("2D ownership highlight tag overflows DataTag");
            result[square] = OwnershipHighlight{
                data::packDataId(mortgaged ? data::LegacyGroupId::Board2 :
                    data::LegacyGroupId::Board, static_cast<data::DataTag>(tag)),
                priority, OwnershipHighlightKind::Bitmap2D, sequence::identity3D()};
        }
        return result;
    }

    std::expected<void, std::string> OwnershipHighlightPlayback::sync(
        const OwnershipHighlightPlan& plan, engine::SequencePlayback& playback)
    {
        std::array<std::shared_ptr<const sequence::SequenceProgram>, rules::SquareCount> programs{};
        std::array<sequence::SequenceTransform, rules::SquareCount> transforms{};
        std::size_t requiredCommands{};

        for (std::size_t square = 0; square < plan.size(); ++square)
        {
            const auto desired = plan[square] ? plan[square]->sequence : data::EmptyDataId;
            if (desired == current_[square]) continue;
            if (current_[square] != data::EmptyDataId) ++requiredCommands;
            if (!plan[square]) continue;
            requiredCommands += 2;
            auto loaded = sequence::SequenceProgram::load(playback.resources(), desired);
            if (!loaded) return std::unexpected(loaded.error().detail);
            programs[square] = std::move(*loaded);

            if (plan[square]->kind == OwnershipHighlightKind::Mesh3D)
            {
                transforms[square] = plan[square]->transform3D;
            }
            else
            {
                const auto bytes = playback.resources()->banks().load(desired);
                if (!bytes) return std::unexpected(bytes.error().detail);
                const auto metadata = data::inspectLegacyUap(**bytes);
                if (!metadata) return std::unexpected(metadata.error().detail);
                transforms[square] = sequence::moveXYTransform(
                    metadata->originX, metadata->originY);
            }
        }

        if (requiredCommands > sequence::SequenceCommandQueue::Capacity ||
            requiredCommands > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
            return std::unexpected(
                "sequence command queue cannot fit board ownership transition");

        for (std::size_t square = 0; square < plan.size(); ++square)
        {
            const auto desired = plan[square] ? plan[square]->sequence : data::EmptyDataId;
            if (desired == current_[square]) continue;
            const auto priority = static_cast<std::uint16_t>(OwnershipPriorityBase + square);
            if (current_[square] != data::EmptyDataId)
                (void)playback.commands().enqueue(sequence::StopSequenceCommand{
                    current_[square], priority, false});
            if (plan[square])
            {
                (void)playback.commands().enqueue(sequence::StartSequenceCommand{
                    programs[square], priority, {}});
                (void)playback.commands().enqueue(sequence::makeMoveTheWorks(
                    desired, priority, transforms[square]));
            }
        }

        for (std::size_t square = 0; square < plan.size(); ++square)
            current_[square] = plan[square] ? plan[square]->sequence : data::EmptyDataId;
        return {};
    }

    void OwnershipHighlightPlayback::reset() noexcept
    {
        current_.fill(data::EmptyDataId);
    }
}
