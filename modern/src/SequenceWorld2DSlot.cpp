#include "SequenceWorld2DSlot.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace monopoly::engine
{
    std::expected<SequenceWorld2DSyncStats, std::string> SequenceWorld2DSlot::sync(
        const std::vector<sequence::SequenceBitmapRenderItem>& items,
        data::BitmapRuntimeCache& cache)
    {
        std::set<sequence::SequenceNodeId> incoming;
        for (const auto& item : items)
        {
            if (!incoming.insert(item.node).second)
                return std::unexpected("duplicate 2D sequence render node");
            if (!std::ranges::all_of(item.worldTransform.values,
                    [](float value) { return std::isfinite(value); }))
                return std::unexpected("non-finite 2D sequence matrix");
        }
        std::map<sequence::SequenceNodeId, SequenceWorld2DObject> next;
        std::vector<sequence::SequenceNodeId> order;
        SequenceWorld2DSyncStats stats;
        for (const auto& item : items)
        {
            auto asset = cache.resolve(item.contentsDataId, item.bytes);
            if (!asset) return std::unexpected(asset.error().detail);
            const auto* old = find(item.node);
            if (!old) ++stats.started;
            else if (old->asset != *asset || old->priority != item.priority ||
                old->worldTransform.values != item.worldTransform.values) ++stats.moved;
            else ++stats.unchanged;
            next.emplace(item.node, SequenceWorld2DObject{item.node, item.contentsDataId,
                item.priority, item.clock, item.worldTransform, *asset});
            order.push_back(item.node);
        }
        for (const auto& [id, object] : objects_)
        {
            (void)object;
            if (!incoming.contains(id)) ++stats.stopped;
        }
        // L_Rend2D.cpp:2730 draws depth-first, then siblings. Runtime has
        // already ordered siblings by priority. A global leaf sort would
        // incorrectly move children across unrelated parent subtrees.
        objects_.swap(next);
        order_.swap(order);
        return stats;
    }
}
