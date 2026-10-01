#include "SequenceWorld2DSlot.hpp"
#include "ModernIBarSkin.hpp"
#include "ModernMenuSkin.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <limits>

namespace monopoly::engine
{
    std::array<std::int32_t, 2> SequenceWorld2DSlot::transformPoint(
        const sequence::Matrix2D& matrix, std::int32_t x, std::int32_t y) noexcept
    {
        const auto& m = matrix.values;
        const float px = static_cast<float>(x), py = static_cast<float>(y);
        const float w = m[2] * px + m[5] * py + m[8];
        if (w == 0.0F) return {};
        const auto rounded = [](float value) {
            if (!std::isfinite(value)) return std::int32_t{};
            return static_cast<std::int32_t>(std::clamp(
                std::nearbyint(static_cast<double>(value)),
                static_cast<double>(std::numeric_limits<std::int32_t>::min()),
                static_cast<double>(std::numeric_limits<std::int32_t>::max())));
        };
        return {rounded((m[0] * px + m[3] * py + m[6]) / w),
            rounded((m[1] * px + m[4] * py + m[7]) / w)};
    }

    std::array<std::int32_t, 2> SequenceWorld2DSlot::boundsCenter(
        const sequence::Matrix2D& matrix,
        const data::Sequence2DBoundingBoxAttribute& bounds) noexcept
    {
        const std::array corners{
            transformPoint(matrix, bounds.left, bounds.top),
            transformPoint(matrix, bounds.right, bounds.top),
            transformPoint(matrix, bounds.left, bounds.bottom),
            transformPoint(matrix, bounds.right, bounds.bottom)};
        std::array<std::int64_t, 2> sum{};
        for (const auto& point : corners)
            for (std::size_t axis = 0; axis < 2; ++axis) sum[axis] += point[axis];
        return {static_cast<std::int32_t>(sum[0] / 4),
            static_cast<std::int32_t>(sum[1] / 4)};
    }

    void SequenceWorld2DSlot::updateCamera(
        const std::optional<sequence::SequenceCamera2DView>& camera) noexcept
    {
        if (!camera) return;
        if (!std::ranges::all_of(camera->worldTransform.values,
                [](float value) { return std::isfinite(value); }) ||
            (camera->scale && !std::isfinite(*camera->scale))) return;
        cameraCenter_ = boundsCenter(camera->worldTransform, camera->bounds);
        const auto origin = transformPoint(camera->worldTransform, 0, 0);
        const auto xAxis = transformPoint(camera->worldTransform, 1'000'000, 0);
        const auto dx = static_cast<double>(xAxis[0]) - origin[0];
        const auto dy = static_cast<double>(xAxis[1]) - origin[1];
        cameraRotation_ = dx != 0.0 || dy != 0.0
            ? static_cast<float>(-std::atan2(dy, dx)) : 0.0F;
        if (camera->scale) cameraScale_ = *camera->scale;
        // L_Rend2D:393: translate origin, scale, rotate, translate viewport.
        auto originShift = sequence::identity2D();
        originShift.values[6] = -static_cast<float>(cameraCenter_[0]);
        originShift.values[7] = -static_cast<float>(cameraCenter_[1]);
        worldToScreen_ = sequence::multiply(originShift,
            sequence::moveXYSRTransform(400, 300, cameraScale_, cameraRotation_));
        // The slot uses identity for the inverse of a singular camera matrix.
        screenToWorld_ = sequence::identity2D();
        if (cameraScale_ != 0.0F)
        {
            const auto inverse = sequence::multiply(sequence::translate2D(-400, -300),
                sequence::moveXYSRTransform(cameraCenter_[0], cameraCenter_[1],
                    1.0F / cameraScale_, -cameraRotation_));
            if (std::ranges::all_of(inverse.values,
                    [](float value) { return std::isfinite(value); }))
                screenToWorld_ = inverse;
        }
    }

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
        // Decode first, then select one shell per actual owning CNK. Decorative
        // child leaves become transparent only after that shell succeeds.
        std::map<sequence::SequenceNodeId, std::shared_ptr<const data::BitmapRuntimeAsset>> assets;
        using Owner = std::pair<data::DataId, sequence::SequenceNodeId>;
        std::map<Owner, sequence::SequenceNodeId> shells;
        for (const auto& item : items)
        {
            auto asset = item.runtimeAsset;
            if (!asset)
            {
                auto decoded = cache.resolve(item.contentsDataId, item.metadata.type, item.bytes);
                if (!decoded) return std::unexpected(decoded.error().detail);
                asset = *decoded;
            }
            assets.emplace(item.node, asset);
            if ((modernSkin_ && modernSkin_->supports(item.rootSequenceDataId)) ||
                (modernMenuSkin_ && modernMenuSkin_->supports(item.rootSequenceDataId)))
            {
                const Owner owner{item.rootSequenceDataId, item.rootSequenceNode};
                const auto it = shells.find(owner);
                const auto area = std::uint64_t(asset->image.width) * asset->image.height;
                if (it == shells.end() || area > std::uint64_t(assets.at(it->second)->image.width) *
                    assets.at(it->second)->image.height)
                    shells[owner] = item.node;
            }
        }
        for (const auto& [owner, node] : shells)
        {
            const auto& item = *std::ranges::find_if(items, [node](const auto& item) { return item.node == node; });
            auto raster = sequence::identity2D();
            if (item.bounds)
            {
                raster.values[0] = float(item.bounds->right-item.bounds->left) / assets.at(node)->image.width;
                raster.values[4] = float(item.bounds->bottom-item.bounds->top) / assets.at(node)->image.height;
                raster.values[6] = float(item.bounds->left); raster.values[7] = float(item.bounds->top);
            }
            else
            { raster.values[6] = float(item.metadata.originX); raster.values[7] = float(item.metadata.originY); }
            const auto rasterWorld = sequence::multiply(raster,item.worldTransform);
            const bool menuOwner = modernMenuSkin_ && modernMenuSkin_->supports(owner.first);
            auto replacement = menuOwner ? modernMenuSkin_->substitute(owner.first, assets.at(node), true) :
                modernSkin_->substitute(owner.first, assets.at(node), true, rasterWorld);
            if (replacement == assets.at(node)) continue;
            assets[node] = std::move(replacement);
            for (const auto& item : items)
                if (item.node != node && Owner{item.rootSequenceDataId, item.rootSequenceNode} == owner)
                    assets[item.node] = menuOwner ?
                        modernMenuSkin_->substitute(owner.first, assets.at(item.node), false) :
                        modernSkin_->substitute(owner.first, assets.at(item.node), false);
        }
        std::map<sequence::SequenceNodeId, SequenceWorld2DObject> next;
        std::vector<sequence::SequenceNodeId> order;
        SequenceWorld2DSyncStats stats;
        for (const auto& item : items)
        {
            auto asset = assets.at(item.node);
            // L_Seqncr.cpp:4318-4427 keeps an explicit CNK bounding rectangle;
            // otherwise it uses the bitmap's origin and dimensions. L_Rend2D
            // transforms those four corners, not an image anchored at (0,0).
            // Apply this mapping before the sequence and camera transformations.
            if (!asset->image.width || !asset->image.height)
                return std::unexpected("zero-sized 2D bitmap asset");
            auto rasterToSequence = sequence::identity2D();
            if (asset->presentationRect)
            {
                const auto& box = *asset->presentationRect;
                if (!std::ranges::all_of(box, [](float v) { return std::isfinite(v); }) ||
                    box[2] <= box[0] || box[3] <= box[1])
                    return std::unexpected("invalid 2D presentation rectangle");
                rasterToSequence.values[0] = (box[2] - box[0]) / asset->image.width;
                rasterToSequence.values[4] = (box[3] - box[1]) / asset->image.height;
                rasterToSequence.values[6] = box[0];
                rasterToSequence.values[7] = box[1];
            }
            else if (item.bounds)
            {
                const auto& box = *item.bounds;
                const auto width = static_cast<std::int64_t>(box.right) - box.left;
                const auto height = static_cast<std::int64_t>(box.bottom) - box.top;
                rasterToSequence.values[0] = static_cast<float>(width) /
                    static_cast<float>(asset->image.width);
                rasterToSequence.values[4] = static_cast<float>(height) /
                    static_cast<float>(asset->image.height);
                rasterToSequence.values[6] = static_cast<float>(box.left);
                rasterToSequence.values[7] = static_cast<float>(box.top);
            }
            else
            {
                // Supersampled presentation derivatives retain the exact intrinsic
                // authored footprint and origin, including UAP offsets.
                if (asset->preferLinearFiltering && item.metadata.width && item.metadata.height)
                {
                    rasterToSequence.values[0] = static_cast<float>(item.metadata.width) / asset->image.width;
                    rasterToSequence.values[4] = static_cast<float>(item.metadata.height) / asset->image.height;
                }
                rasterToSequence.values[6] = static_cast<float>(item.metadata.originX);
                rasterToSequence.values[7] = static_cast<float>(item.metadata.originY);
            }
            const auto positioned = sequence::multiply(rasterToSequence, item.worldTransform);
            if (!std::ranges::all_of(positioned.values,
                    [](float value) { return std::isfinite(value); }))
                return std::unexpected("non-finite 2D bitmap placement");
            const auto* old = find(item.node);
            if (!old) ++stats.started;
            else if (old->asset != asset || old->priority != item.priority ||
                old->worldTransform.values != positioned.values) ++stats.moved;
            else ++stats.unchanged;
            next.emplace(item.node, SequenceWorld2DObject{item.node, item.contentsDataId,
                item.priority, item.clock, positioned, asset});
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
