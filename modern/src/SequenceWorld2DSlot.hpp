#pragma once
#include "BitmapRuntime.hpp"
#include "RenderSlots.hpp"
#include "SequenceBitmapRenderData.hpp"
#include <map>

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
        void clear() noexcept { objects_.clear(); order_.clear(); }
        [[nodiscard]] std::size_t size() const noexcept { return objects_.size(); }
        [[nodiscard]] const SequenceWorld2DObject* find(sequence::SequenceNodeId id) const noexcept
        { const auto it = objects_.find(id); return it == objects_.end() ? nullptr : &it->second; }
        [[nodiscard]] const std::vector<sequence::SequenceNodeId>& order() const noexcept
        { return order_; }
    private:
        std::map<sequence::SequenceNodeId, SequenceWorld2DObject> objects_;
        std::vector<sequence::SequenceNodeId> order_;
    };
}
