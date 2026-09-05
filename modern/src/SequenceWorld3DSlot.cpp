#include "SequenceWorld3DSlot.hpp"

#include <set>
#include <utility>

namespace monopoly::engine
{
    void SequenceWorld3DSlot::refreshBounds(SequenceWorld3DObject& object) noexcept
    {
        object.screenBounds.reset();
        if (!view_ || !object.asset || !object.asset->renderData) return;
        object.screenBounds = world3DMeshScreenRect(
            object.asset->renderData->bounds, object.worldTransform, *view_);
    }

    void SequenceWorld3DSlot::refreshAllBounds() noexcept
    {
        for (auto& [node, object] : objects_)
        {
            (void)node;
            refreshBounds(object);
        }
    }

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
                auto [inserted, created] = next.emplace(item.node,
                    SequenceWorld3DObject{item.node, item.contentsDataId,
                        item.priority, item.clock, item.worldTransform,
                        item.asset, std::nullopt});
                (void)created;
                refreshBounds(inserted->second);
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
            if (moved)
            {
                refreshBounds(found->second);
                ++stats.moved;
            }
            else ++stats.unchanged;
        }

        objects_ = std::move(next);
        order_.clear();
        order_.reserve(items.size());
        for (const auto& item : items) order_.push_back(item.node);
        return stats;
    }

    std::expected<std::size_t, World3DProjectionError>
    SequenceWorld3DSlot::configureView(World3DRect viewport, World3DCamera camera)
    {
        auto next = makeWorld3DProjectionState(viewport, camera);
        if (!next) return std::unexpected(next.error());
        view_ = std::move(*next);
        refreshAllBounds();
        return objects_.size();
    }

    void SequenceWorld3DSlot::clearView() noexcept
    {
        view_.reset();
        for (auto& [node, object] : objects_)
        {
            (void)node;
            object.screenBounds.reset();
        }
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

    std::vector<sequence::SequenceNodeId> SequenceWorld3DSlot::visibleOrder() const
    {
        std::vector<sequence::SequenceNodeId> result;
        result.reserve(order_.size());
        for (const auto node : order_)
        {
            const auto found = objects_.find(node);
            if (found != objects_.end() && found->second.screenBounds)
                result.push_back(node);
        }
        return result;
    }

    const std::optional<World3DProjectionState>&
    SequenceWorld3DSlot::view() const noexcept
    { return view_; }
}
