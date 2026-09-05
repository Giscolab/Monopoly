#include "SequenceWorld3DSlot.hpp"

#include <set>
#include <utility>

namespace monopoly::engine
{
    std::expected<SequenceWorld3DSyncStats, SequenceWorld3DSlotError>
    SequenceWorld3DSlot::sync(
        const std::vector<sequence::SequenceMeshRenderItem>& items)
    {
        std::set<sequence::SequenceNodeId> incoming;
        for (const auto& item : items)
        {
            if (!incoming.insert(item.node).second)
                return std::unexpected(SequenceWorld3DSlotError{
                    SequenceWorld3DSlotErrorCode::DuplicateNode, item.node});
        }

        auto next = objects_;
        SequenceWorld3DSyncStats stats;
        for (auto it = next.begin(); it != next.end();)
        {
            if (!incoming.contains(it->first))
            {
                it = next.erase(it);
                ++stats.stopped;
            }
            else ++it;
        }
        for (const auto& item : items)
        {
            const auto found = next.find(item.node);
            if (found == next.end())
            {
                next.emplace(item.node, SequenceWorld3DObject{
                    item.node, item.contentsDataId, item.priority, item.clock,
                    item.worldTransform, item.asset});
                ++stats.started;
                continue;
            }

            const bool moved =
                found->second.contentsDataId != item.contentsDataId ||
                found->second.asset != item.asset ||
                found->second.worldTransform.values != item.worldTransform.values;
            found->second.contentsDataId = item.contentsDataId;
            found->second.priority = item.priority;
            found->second.clock = item.clock;
            found->second.worldTransform = item.worldTransform;
            found->second.asset = item.asset;
            if (moved) ++stats.moved;
            else ++stats.unchanged;
        }

        objects_ = std::move(next);
        order_.clear();
        order_.reserve(items.size());
        for (const auto& item : items) order_.push_back(item.node);
        return stats;
    }
    void SequenceWorld3DSlot::clear() noexcept
    {
        objects_.clear();
        order_.clear();
    }

    std::size_t SequenceWorld3DSlot::size() const noexcept
    { return objects_.size(); }

    const SequenceWorld3DObject* SequenceWorld3DSlot::find(
        sequence::SequenceNodeId node) const noexcept
    {
        const auto found = objects_.find(node);
        return found == objects_.end() ? nullptr : &found->second;
    }

    std::vector<sequence::SequenceNodeId> SequenceWorld3DSlot::order() const
    { return order_; }
}
