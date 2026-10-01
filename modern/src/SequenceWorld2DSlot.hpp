#pragma once
#include "BitmapRuntime.hpp"
#include "RenderSlots.hpp"
#include "SequenceBitmapRenderData.hpp"
#include <map>

namespace monopoly::ibar { class ModernIBarSkin; }

namespace monopoly::engine
{
    struct SequenceWorld2DObject
    {
        sequence::SequenceNodeId node{};
        data::DataId contentsDataId{};
        std::uint16_t priority{};
        std::int32_t clock{};
        sequence::Matrix2D worldTransform{};
        std::shared_ptr<const data::BitmapRuntimeAsset> asset;
    };
    struct SequenceWorld2DSyncStats
    {
        std::size_t started{}, moved{}, stopped{}, unchanged{};
    };
    class SequenceWorld2DSlot final
    {
    public:
        [[nodiscard]] static constexpr RenderSlot slot() noexcept
        { return RenderSlot::Overlay2D; }
        [[nodiscard]] std::expected<SequenceWorld2DSyncStats, std::string> sync(
            const std::vector<sequence::SequenceBitmapRenderItem>& items,
            data::BitmapRuntimeCache& cache);
        // nullptr preserves the complete legacy bitmap path. The skin changes pixels only.
        void configureModernIBarSkin(std::shared_ptr<ibar::ModernIBarSkin> skin) noexcept
        { modernSkin_ = std::move(skin); }
        // Removing sequence objects does not uninstall the legacy render slot:
        // its last camera remains until another label owner supplies a view.
        void clear() noexcept { objects_.clear(); order_.clear(); }
        void updateCamera(const std::optional<sequence::SequenceCamera2DView>& camera) noexcept;
        [[nodiscard]] const sequence::Matrix2D& worldToScreen() const noexcept
        { return worldToScreen_; }
        [[nodiscard]] const sequence::Matrix2D& screenToWorld() const noexcept
        { return screenToWorld_; }
        [[nodiscard]] static constexpr std::uint8_t cameraLabel() noexcept { return 1; }
        [[nodiscard]] static std::array<std::int32_t, 2> transformPoint(
            const sequence::Matrix2D& matrix, std::int32_t x, std::int32_t y) noexcept;
        [[nodiscard]] static std::array<std::int32_t, 2> boundsCenter(
            const sequence::Matrix2D& matrix,
            const data::Sequence2DBoundingBoxAttribute& bounds) noexcept;
        [[nodiscard]] std::size_t size() const noexcept { return objects_.size(); }
        [[nodiscard]] const SequenceWorld2DObject* find(sequence::SequenceNodeId id) const noexcept
        { const auto it = objects_.find(id); return it == objects_.end() ? nullptr : &it->second; }
        [[nodiscard]] const std::vector<sequence::SequenceNodeId>& order() const noexcept
        { return order_; }
    private:
        std::array<std::int32_t, 2> cameraCenter_{400, 300};
        float cameraRotation_{};
        float cameraScale_{1.0F};
        sequence::Matrix2D worldToScreen_{sequence::identity2D()};
        sequence::Matrix2D screenToWorld_{sequence::identity2D()};
        std::shared_ptr<ibar::ModernIBarSkin> modernSkin_;
        std::map<sequence::SequenceNodeId, SequenceWorld2DObject> objects_;
        std::vector<sequence::SequenceNodeId> order_;
    };
}
