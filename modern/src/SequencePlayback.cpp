#include "SequencePlayback.hpp"
#include "BoardTextureRuntime.hpp"
#include <algorithm>

namespace monopoly::engine
{
    namespace
    {
        std::array<float, 2> transform2DPoint(
            const sequence::Matrix2D& matrix, float x, float y) noexcept
        {
            const auto& m = matrix.values;
            const float outX = m[0] * x + m[3] * y + m[6];
            const float outY = m[1] * x + m[4] * y + m[7];
            const float outW = m[2] * x + m[5] * y + m[8];
            if (outW == 0.0F) return {};
            return {outX / outW, outY / outW};
        }

        bool scrollingWorld2DVisible(
            const sequence::SequenceScrollingWorldView& view) noexcept
        {
            if (!view.bounds2D ||
                !std::holds_alternative<sequence::Matrix2D>(view.worldTransform))
                return true;

            const auto& box = *view.bounds2D;
            const auto& matrix =
                std::get<sequence::Matrix2D>(view.worldTransform);
            const std::array points{
                transform2DPoint(matrix,
                    static_cast<float>(box.left),
                    static_cast<float>(box.top)),
                transform2DPoint(matrix,
                    static_cast<float>(box.right),
                    static_cast<float>(box.top)),
                transform2DPoint(matrix,
                    static_cast<float>(box.left),
                    static_cast<float>(box.bottom)),
                transform2DPoint(matrix,
                    static_cast<float>(box.right),
                    static_cast<float>(box.bottom))};

            float left = points.front()[0];
            float right = left;
            float top = points.front()[1];
            float bottom = top;
            for (const auto& point : points)
            {
                left = std::min(left, point[0]);
                right = std::max(right, point[0]);
                top = std::min(top, point[1]);
                bottom = std::max(bottom, point[1]);
            }
            return right > 0.0F && bottom > 0.0F &&
                left < 800.0F && top < 600.0F;
        }
    }
    void SequencePlayback::setEuropeanDeeds(const std::array<data::DataId, 56>& ids)
    {
        for (const auto id : europeanDeeds_)
            if (id != data::EmptyDataId && std::find(ids.begin(), ids.end(), id) == ids.end())
                retiredDeeds_.push_back(id);
        europeanDeeds_ = ids;
    }

    data::DataId SequencePlayback::deedDataId(int square, bool front,
        data::DataId staticFallback) const noexcept
    {
        const auto snapshot = resources();
        if (!snapshot || snapshot->context().board == data::BoardEdition::Usa)
            return staticFallback;
        // DISPLAY_propertyToOwnablePropertyConversion: board order, not TRANS_PROP.
        static constexpr std::array<int, 28> squares{
            1,3,5,6,8,9,11,12,13,14,15,16,18,19,
            21,23,24,25,26,27,28,29,31,32,34,35,37,39};
        const auto found = std::find(squares.begin(), squares.end(), square);
        if (found == squares.end()) return data::EmptyDataId;
        const auto property = std::distance(squares.begin(), found);
        return europeanDeeds_[static_cast<std::size_t>(property + (front ? 0 : 28))];
    }

    std::expected<data::DataId, std::string> SequencePlayback::createVideoObject(
        std::string fileName, data::SequenceVideoData options,
        data::Sequence2DBoundingBoxAttribute bounds,
        bool binkDoubleSize)
    {
        for (std::uint32_t attempt = 0; attempt < 0xFFFFU; ++attempt)
        {
            if (nextRuntimeVideoTag_ == 0)
                nextRuntimeVideoTag_ = 1;
            const auto id = data::packDataId(
                RuntimeVideoGroup, nextRuntimeVideoTag_++);
            if (runtimePrograms_.contains(id))
                continue;

            auto program = sequence::SequenceProgram::runtimeVideo(
                id, std::move(fileName), options, bounds, binkDoubleSize);
            if (!program)
                return std::unexpected(program.error().detail);
            runtimePrograms_.emplace(id, *program);
            return id;
        }
        return std::unexpected("runtime video DataID space exhausted");
    }

    bool SequencePlayback::freeRuntimeSequence(data::DataId id) noexcept
    {
        return runtimePrograms_.erase(id) != 0;
    }

    std::expected<void, std::string> SequencePlayback::configureBoardTextures(
        data::BoardMeshKind mesh, data::TextureResolution resolution,
        int city, int currency, const std::filesystem::path& customRoot)
    {
        const auto resources = meshes_.resources();
        if (!resources)
            return std::unexpected("board textures require a resource snapshot");
        const auto context = resources->context();
        const BoardTextureSelection selection{
            mesh, resolution, context.board, context.language, city, currency, customRoot};
        if (boardTextureSelection_ == selection) return {};

        const auto recipe = context.board == data::BoardEdition::Usa
            ? data::buildUsaTextureRecipe(mesh, resolution)
            : data::buildEuropeanTextureRecipe(mesh, resolution);
        if (!recipe) return std::unexpected(std::string(recipe.error().detail));
        const auto images = data::loadBoardTextureImages(resources->paths(), *recipe,
            {context.board, context.language, city, currency, customRoot});
        if (!images) return std::unexpected(images.error());
        const auto replaced = meshes_.replaceTextureImages(recipe->meshDataId, *images);
        if (!replaced) return std::unexpected(replaced.error().detail);
        boardTextureSelection_ = selection;
        return {};
    }

    std::expected<std::shared_ptr<const sequence::SequenceProgram>, std::string>
    SequencePlayback::loadProgram(data::DataId id)
    {
        if (const auto found = runtimePrograms_.find(id);
            found != runtimePrograms_.end())
            return found->second;
        if (runtimeBitmaps_.contains(id))
        {
            auto program = sequence::SequenceProgram::rawBitmap(
                id, data::LegacyDataType::Native);
            if (!program) return std::unexpected(program.error().detail);
            return *program;
        }
        auto program = sequence::SequenceProgram::load(meshes_.resources(), id);
        if (!program) return std::unexpected(program.error().detail);
        return *program;
    }

    std::expected<void, std::string> SequencePlayback::start(
        data::DataId id, std::uint16_t priority, std::uint8_t labelOverride)
    {
        auto program = loadProgram(id);
        if (!program) return std::unexpected(program.error());
        const auto queued = commands_.enqueue(sequence::StartSequenceCommand{
            *program, priority, {}, std::nullopt, labelOverride});
        if (!queued)
            return std::unexpected("sequence command queue capacity exceeded");
        return {};
    }

    std::expected<void, std::string> SequencePlayback::startXY(
        data::DataId id, std::uint16_t priority,
        std::int32_t x, std::int32_t y, bool dropFrames,
        std::uint8_t labelOverride)
    {
        auto program = loadProgram(id);
        if (!program) return std::unexpected(program.error());
        sequence::ClockStartOptions options{};
        options.dropFrames = dropFrames;
        const auto queued = commands_.enqueue(sequence::StartSequenceCommand{
            *program, priority, options, sequence::moveXYTransform(x, y),
            labelOverride});
        if (!queued) return std::unexpected("sequence command queue capacity exceeded");
        return {};
    }

    std::expected<void, std::string> SequencePlayback::startXYDrop(
        data::DataId id, std::uint16_t priority,
        std::int32_t x, std::int32_t y, bool dropFrames,
        std::uint8_t labelOverride)
    {
        return startXY(id, priority, x, y, dropFrames, labelOverride);
    }

    std::expected<void, std::string> SequencePlayback::startXYSR(
        data::DataId id, std::uint16_t priority,
        std::int32_t x, std::int32_t y,
        float scale, float rotate)
    {
        auto program = loadProgram(id);
        if (!program) return std::unexpected(program.error());
        const auto queued = commands_.enqueue(sequence::StartSequenceCommand{
            *program, priority, {},
            sequence::SequenceTransform(
                sequence::moveXYSRTransform(x, y, scale, rotate))});
        if (!queued)
            return std::unexpected(
                "sequence command queue capacity exceeded");
        return {};
    }

    std::expected<void, std::string> SequencePlayback::startRySTxz(
        data::DataId id, std::uint16_t priority,
        float yaw, float scale, float x, float z)
    {
        auto program = loadProgram(id);
        if (!program) return std::unexpected(program.error());
        const auto queued = commands_.enqueue(sequence::StartSequenceCommand{
            *program, priority, {},
            sequence::SequenceTransform(
                sequence::moveRySTxzTransform(yaw, scale, x, z))});
        if (!queued)
            return std::unexpected(
                "sequence command queue capacity exceeded");
        return {};
    }

    std::expected<void, std::string> SequencePlayback::startRySTxzDrop(
        data::DataId id, std::uint16_t priority,
        float yaw, float scale, float x, float z,
        bool dropFrames)
    {
        auto program = loadProgram(id);
        if (!program) return std::unexpected(program.error());
        sequence::ClockStartOptions options{};
        options.dropFrames = dropFrames;
        const auto queued = commands_.enqueue(sequence::StartSequenceCommand{
            *program, priority, options,
            sequence::SequenceTransform(
                sequence::moveRySTxzTransform(yaw, scale, x, z))});
        if (!queued)
            return std::unexpected(
                "sequence command queue capacity exceeded");
        return {};
    }


    std::expected<void, std::string> SequencePlayback::transitionXY(
        std::optional<data::DataId> previousId, data::DataId id,
        std::uint16_t priority, std::int32_t x, std::int32_t y,
        bool dropFrames)
    {
        auto program = loadProgram(id);
        if (!program) return std::unexpected(program.error());
        const std::size_t required = previousId ? 2U : 1U;
        if (commands_.pendingCount() > sequence::SequenceCommandQueue::Capacity - required)
            return std::unexpected("sequence command queue capacity exceeded");
        if (previousId)
        {
            const auto stopped = commands_.enqueue(
                sequence::StopSequenceCommand{*previousId, priority});
            if (!stopped) return std::unexpected("sequence command queue capacity exceeded");
        }
        sequence::ClockStartOptions options{};
        options.dropFrames = dropFrames;
        auto queued = commands_.enqueue(sequence::StartSequenceCommand{
            *program, priority, options, sequence::moveXYTransform(x, y)});
        if (!queued) return std::unexpected("sequence command queue capacity exceeded");
        return {};
    }
    std::expected<void, std::string> SequencePlayback::setEndingAction(
        data::DataId id, std::uint16_t priority, std::uint8_t action)
    {
        if (action == 0 || action > 3)
            return std::unexpected("invalid sequence ending action");
        const auto queued = commands_.enqueue(sequence::SetSequenceEndingActionCommand{
            id, priority, action, false});
        if (!queued) return std::unexpected("sequence command queue rejected ending action");
        return {};
    }

    std::expected<void, std::string> SequencePlayback::setVolume(
        data::DataId id, std::uint16_t priority, std::uint8_t volume)
    {
        const auto queued = commands_.enqueue(sequence::SetSequenceVolumeCommand{
            id, priority, volume, false});
        if (!queued)
            return std::unexpected(
                "sequence command queue rejected volume change");
        return {};
    }


    std::expected<void, std::string> SequencePlayback::setPitch(
        data::DataId id, std::uint16_t priority, std::uint16_t pitch)
    {
        const auto queued = commands_.enqueue(
            sequence::SetSequencePitchCommand{
                id, priority, pitch, false});
        if (!queued)
            return std::unexpected(
                "sequence command queue rejected pitch change");
        return {};
    }

    std::expected<void, std::string> SequencePlayback::setPanning(
        data::DataId id, std::uint16_t priority, std::int8_t panning)
    {
        const auto queued = commands_.enqueue(
            sequence::SetSequencePanningCommand{
                id, priority, panning, false});
        if (!queued)
            return std::unexpected(
                "sequence command queue rejected panning change");
        return {};
    }

    std::expected<void, std::string> SequencePlayback::forceRedraw(
        data::DataId id, std::uint16_t priority)
    {
        const auto queued = commands_.enqueue(
            sequence::ForceRedrawSequenceCommand{id, priority, false});
        if (!queued)
            return std::unexpected(
                "sequence command queue rejected redraw");
        return {};
    }

    std::expected<void, std::string> SequencePlayback::startMoved(
        data::DataId id, std::uint16_t priority,
        sequence::SequenceTransform transform)
    {
        auto program = loadProgram(id);
        if (!program) return std::unexpected(program.error());
        const auto queued = commands_.enqueue(sequence::StartSequenceCommand{
            *program, priority, {}, std::move(transform)});
        if (!queued) return std::unexpected("sequence command queue capacity exceeded");
        return {};
    }

    std::expected<void, std::string> SequencePlayback::stop(
        data::DataId id, std::uint16_t priority)
    {
        const auto queued = commands_.enqueue(
            sequence::StopSequenceCommand{id, priority});
        if (!queued) return std::unexpected("sequence command queue capacity exceeded");
        return {};
    }

    std::expected<void, std::string> SequencePlayback::move(
        data::DataId id, std::uint16_t priority,
        sequence::SequenceTransform transform)
    {
        const auto queued = commands_.enqueue(sequence::makeMoveTheWorks(
            id, priority, std::move(transform)));
        if (!queued) return std::unexpected("sequence command queue capacity exceeded");
        return {};
    }

    std::expected<void, std::string> SequencePlayback::transitionMovedDrop(
        std::optional<data::DataId> previousId, data::DataId id,
        std::uint16_t priority, sequence::SequenceTransform transform,
        std::uint8_t endingAction)
    {
        auto program = loadProgram(id);
        if (!program) return std::unexpected(program.error());
        if (endingAction == 0 || endingAction > 3)
            return std::unexpected("invalid sequence ending action");

        const std::size_t required = previousId ? 3U : 2U;
        if (commands_.pendingCount() > sequence::SequenceCommandQueue::Capacity - required)
            return std::unexpected("sequence command queue capacity exceeded");

        if (previousId)
        {
            auto queued = commands_.enqueue(
                sequence::StopSequenceCommand{*previousId, priority});
            if (!queued) return std::unexpected("sequence command queue capacity exceeded");
        }

        sequence::ClockStartOptions options{};
        options.dropFrames = true;
        auto queued = commands_.enqueue(sequence::StartSequenceCommand{
            *program, priority, options, std::move(transform)});
        if (!queued) return std::unexpected("sequence command queue capacity exceeded");
        queued = commands_.enqueue(sequence::SetSequenceEndingActionCommand{
            id, priority, endingAction, false});
        if (!queued) return std::unexpected("sequence command queue rejected ending action");
        return {};
    }

    std::expected<void, std::string> SequencePlayback::transitionRySTxzDropStayAtEnd(
        std::optional<data::DataId> previousId, data::DataId id,
        std::uint16_t priority, float yaw, float scale, float x, float z)
    {
        return transitionMovedDrop(previousId, id, priority,
            sequence::makeMoveRySTxz(id, priority, yaw, scale, x, z).transform,
            2);
    }

    std::expected<void, std::string> SequencePlayback::setViewport3D(
        World3DRect viewport)
    {
        const auto queued = commands_.enqueue(sequence::SetViewportCommand{
            static_cast<std::uint8_t>(RenderSlot::World3D),
            {viewport.left, viewport.top, viewport.right, viewport.bottom}});
        if (!queued)
            return std::unexpected(
                "sequence command queue rejected World3D viewport");
        return {};
    }

    std::expected<void, std::string> SequencePlayback::setCamera3D(
        const World3DCamera& camera)
    {
        const auto queued = commands_.enqueue(sequence::SetCameraCommand{
            static_cast<std::uint8_t>(RenderSlot::World3D), 0,
            camera.location, camera.forward, camera.up, camera.fieldOfView,
            camera.nearPlane, camera.farPlane});
        if (!queued) return std::unexpected("sequence command queue rejected World3D camera");
        return {};
    }

    std::expected<void, std::string> SequencePlayback::setCameraNumber(
        std::uint8_t cameraNumber)
    {
        const auto queued = commands_.enqueue(sequence::SetCameraCommand{
            static_cast<std::uint8_t>(RenderSlot::World3D), cameraNumber,
            {0.0F, 0.0F, -500.0F}, {0.0F, 0.0F, 1.0F},
            {0.0F, 1.0F, 0.0F}, 0.785398F, 1.0F, 1000.0F});
        if (!queued) return std::unexpected("sequence command queue rejected camera label");
        return {};
    }

    std::expected<void, std::string> SequencePlayback::publishRuntimeViews()
    {
        for (const auto& outcome : commands_.outcomes())
            if (outcome.error)
            {
                world_.clear();
                world2D_.clear();
                return std::unexpected(outcome.error->detail);
            }

        auto items = sequence::collectSequenceMeshRenderData(runtime_, meshes_);
        if (!items)
        {
            world_.clear();
            world2D_.clear();
            return std::unexpected(items.error().cause.detail);
        }

        const auto bitmapItems = sequence::collectSequenceBitmapRenderData(
            runtime_, meshes_.resources(), &runtimeBitmaps_);
        if (!bitmapItems)
        {
            world_.clear();
            world2D_.clear();
            return std::unexpected(bitmapItems.error().detail);
        }

        const auto bitmapPublished = world2D_.sync(*bitmapItems, bitmaps_);
        if (!bitmapPublished)
        {
            world_.clear();
            world2D_.clear();
            return std::unexpected(bitmapPublished.error());
        }

        const auto published = world_.sync(*items);
        if (!published)
        {
            world_.clear();
            world2D_.clear();
            return std::unexpected("duplicate sequence render node");
        }

        for (const auto& scrolling : runtime_.scrollingWorldInstances())
        {
            bool visible = true;
            if (scrolling.dimensionality == 2)
                visible = scrollingWorld2DVisible(scrolling);
            else if (scrolling.dimensionality == 3 &&
                     scrolling.bounds3D &&
                     std::holds_alternative<sequence::Matrix3D>(
                         scrolling.worldTransform))
                visible = world_.boundsVisible(
                    *scrolling.bounds3D,
                    std::get<sequence::Matrix3D>(
                        scrolling.worldTransform));

            if (visible == scrolling.onScreen)
                continue;
            const auto changed = runtime_.setScrollingWorldVisibility(
                scrolling.node, visible);
            if (!changed)
                return std::unexpected(changed.error().detail);
        }

        // Keep the old immutable surfaces until consumers have stopped them.
        std::erase_if(retiredDeeds_, [&](data::DataId id)
        {
            if (std::any_of(bitmapItems->begin(), bitmapItems->end(),
                    [id](const auto& item) { return item.contentsDataId == id; }))
                return false;
            (void)runtimeBitmaps_.remove(id);
            return true;
        });
        return {};
    }

    std::expected<int, std::string> SequencePlayback::collectCommands()
    {
        const auto result = commands_.collect();
        if (!result)
            return std::unexpected("sequence command collection nesting overflow");
        return *result;
    }

    std::expected<int, std::string> SequencePlayback::executeCommands()
    {
        const auto result = commands_.execute();
        if (!result)
            return std::unexpected("sequence command execution nesting overflow");
        if (const auto& cycleError = commands_.lastCycleError())
            return std::unexpected(cycleError->detail);
        for (const auto& outcome : commands_.outcomes())
            if (outcome.error)
                return std::unexpected(outcome.error->detail);
        return *result;
    }

    std::expected<void, std::string> SequencePlayback::processUserCommands()
    {
        const auto updated = commands_.processUserCommands();
        if (!updated)
        {
            world_.clear();
            world2D_.clear();
            return std::unexpected(updated.error().detail);
        }
        return publishRuntimeViews();
    }

    std::expected<void, std::string> SequencePlayback::stopAll()
    {
        const auto flushed = processUserCommands();
        if (!flushed) return flushed;
        runtime_.stopAll();
        world_.clear();
        world2D_.clear();
        runtimePrograms_.clear();
        retiredDeeds_.clear();
        return {};
    }

    std::expected<void, std::string> SequencePlayback::update(std::int32_t tick)
    {
        const auto updated = commands_.updateCycle(tick);
        if (!updated)
        {
            world_.clear();
            world2D_.clear();
            return std::unexpected(updated.error().detail);
        }
        return publishRuntimeViews();
    }
}
