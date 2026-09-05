#include "SequenceWorld3DSlot.hpp"

#include <cmath>
#include <set>
#include <utility>

namespace monopoly::engine
{
    namespace
    {
        std::array<float, 3> sequencePoint(const sequence::Matrix3D& matrix,
            float x, float y, float z) noexcept
        {
            const auto& m = matrix.values;
            const float outX = m[0] * x + m[4] * y + m[8] * z + m[12];
            const float outY = m[1] * x + m[5] * y + m[9] * z + m[13];
            const float outZ = m[2] * x + m[6] * y + m[10] * z + m[14];
            const float outW = m[3] * x + m[7] * y + m[11] * z + m[15];
            if (outW == 0.0F) return {};
            return {outX / outW, outY / outW, outZ / outW};
        }
        std::array<float, 3> difference(const std::array<float, 3>& a,
            const std::array<float, 3>& b) noexcept
        { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
        std::array<float, 3> normalized(const std::array<float, 3>& value) noexcept
        {
            const float length = std::sqrt(value[0] * value[0] +
                value[1] * value[1] + value[2] * value[2]);
            if (length == 0.0F) return {};
            return {value[0] / length, value[1] / length, value[2] / length};
        }
    }

    World3DCamera world3DCameraFromSequence(
        const sequence::SequenceCamera3DView& camera) noexcept
    {
        const auto location = sequencePoint(camera.worldTransform, 0.0F, 0.0F, 0.0F);
        const auto forwardPoint = sequencePoint(camera.worldTransform, 0.0F, 0.0F, 1.0F);
        const auto upPoint = sequencePoint(camera.worldTransform, 0.0F, 1.0F, 0.0F);
        return World3DCamera{location,
            normalized(difference(forwardPoint, location)),
            normalized(difference(upPoint, location)), camera.fieldOfView,
            camera.nearPlane, camera.farPlane};
    }

    std::optional<World3DCamera> resolveWorld3DCamera(
        const sequence::SetCameraCommand& command,
        const sequence::SequenceRuntime& runtime) noexcept
    {
        if (command.cameraNumber == 0)
            return World3DCamera{command.position, command.forwards, command.up,
                command.fieldOfView, command.nearPlane, command.farPlane};

        const auto camera = runtime.cameraForLabel(command.cameraNumber);
        if (!camera) return std::nullopt;
        return world3DCameraFromSequence(*camera);
    }

    void SequenceWorld3DSlot::refreshBounds(SequenceWorld3DObject& object) noexcept
    {
        object.screenBounds.reset();
        if (!view_ || !object.asset) return;
        const auto renderData = object.renderData ? object.renderData : object.asset->renderData;
        if (!renderData) return;
        object.screenBounds = world3DMeshScreenRect(
            renderData->bounds, object.worldTransform, *view_);
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
                        item.asset, std::nullopt, item.renderData});
                (void)created;
                refreshBounds(inserted->second);
                ++stats.started;
                continue;
            }

            const bool moved =
                found->second.contentsDataId != item.contentsDataId ||
                found->second.asset != item.asset ||
                found->second.renderData != item.renderData ||
                found->second.worldTransform.values != item.worldTransform.values;
            found->second.contentsDataId = item.contentsDataId;
            found->second.priority = item.priority;
            found->second.clock = item.clock;
            found->second.worldTransform = item.worldTransform;
            found->second.asset = item.asset;
            found->second.renderData = item.renderData;
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
