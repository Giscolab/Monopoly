#include "BoardBackdropPlayback.hpp"

#include "LegacyBitmap.hpp"

#include <algorithm>
#include <fstream>
#include <utility>
#include <vector>

namespace monopoly::boarddisplay
{
    namespace
    {
        [[nodiscard]] std::expected<std::uint32_t, std::string> cameraIndex(
            pieces::BoardCameraView camera)
        {
            const auto value = static_cast<std::uint32_t>(camera);
            if (value >= BoardCameraCount)
                return std::unexpected("UDBoard backdrop camera is outside 0..38");
            return value;
        }

        [[nodiscard]] std::expected<std::uint32_t, std::string> cityIndex(
            int city)
        {
            if (city < 0 || city >= static_cast<int>(UsaCityCount))
                return std::unexpected("UDBoard backdrop city is outside 0..10");
            return static_cast<std::uint32_t>(city);
        }

        [[nodiscard]] data::DataId boardBitmapId(
            data::DataTag base, std::uint32_t city, std::uint32_t camera) noexcept
        {
            const auto boardIndex = city * BoardsPerCity + camera;
            return data::packDataId(data::LegacyGroupId::Board,
                static_cast<data::DataTag>(base + boardIndex));
        }

        [[nodiscard]] data::LegacyBitmapRGBA8 opaqueBlack(
            std::uint32_t width, std::uint32_t height)
        {
            data::LegacyBitmapRGBA8 image{width, height, {}};
            image.pixels.resize(static_cast<std::size_t>(width) * height * 4U, 0);
            for (std::size_t offset = 3; offset < image.pixels.size(); offset += 4)
                image.pixels[offset] = 255;
            return image;
        }

        [[nodiscard]] std::pair<std::int32_t, std::int32_t> backdropPosition(
            display::Screen2D view) noexcept
        {
            switch (view)
            {
            case display::Screen2D::Trade: return {200, 0};
            case display::Screen2D::Main:
            case display::Screen2D::Portfolio: return {0, 0};
            default: return {0, 0};
            }
        }
    }

    std::expected<void, std::string> BoardBackdropPlayback::ensureSurfaces(
        engine::SequencePlayback& playback)
    {
        if (surfacesReady_) return {};

        std::vector<data::DataId> allocated;
        allocated.reserve(MainBoardBufferCount + 1);
        for (auto& buffer : mainBuffers_)
        {
            const auto created = playback.runtimeBitmaps().create(
                MainBoardWidth, MainBoardHeight, false);
            if (!created)
            {
                for (const auto id : allocated) (void)playback.runtimeBitmaps().remove(id);
                return std::unexpected(created.error());
            }
            buffer.surface = *created;
            buffer.cityLoaded = -1;
            buffer.viewLoaded = -1;
            buffer.timeLoaded = 0;
            buffer.customRootLoaded.clear();
            allocated.push_back(*created);
        }

        const auto trade = playback.runtimeBitmaps().create(
            TradeBoardWidth, TradeBoardHeight, false);
        if (!trade)
        {
            for (const auto id : allocated) (void)playback.runtimeBitmaps().remove(id);
            mainBuffers_ = {};
            return std::unexpected(trade.error());
        }
        tradeSurface_ = *trade;
        surfacesReady_ = true;
        return {};
    }

    std::expected<data::LegacyBitmapRGBA8, std::string>
    BoardBackdropPlayback::loadBoardBitmap(
        data::DataId id, engine::SequencePlayback& playback) const
    {
        const auto resources = playback.resources();
        if (!resources) return std::unexpected("UDBoard backdrop has no resource snapshot");
        const auto metadata = resources->banks().metadata(id);
        if (!metadata) return std::unexpected(metadata.error().detail);
        if (metadata->type != data::LegacyDataType::Bitmap)
            return std::unexpected("UDBoard backdrop source is not DataBMP");

        const auto bytes = resources->banks().load(id);
        if (!bytes) return std::unexpected(bytes.error().detail);
        const auto decoded = data::decodeLegacyBitmapRGBA8(**bytes);
        if (!decoded) return std::unexpected(decoded.error().detail);
        return *decoded;
    }
    std::expected<data::LegacyBitmapRGBA8, std::string>
    BoardBackdropPlayback::loadCustomBoardBitmap(
        std::uint32_t camera, const std::filesystem::path& customRoot,
        engine::SequencePlayback& playback) const
    {
        const auto resources = playback.resources();
        if (!resources) return std::unexpected("custom UDBoard backdrop has no resource snapshot");
        const auto names = data::twoDimensionalBoardTextureNames();
        if (camera >= names.size())
            return std::unexpected("custom UDBoard backdrop camera is outside 0..38");
        const data::BoardTextureContext context{
            resources->context().board, resources->context().language,
            -1, 0, customRoot};
        const auto resolved = data::resolveBoardTexturePath(resources->paths(),
            data::BoardMeshKind::CityMedium, data::TextureLocation::CustomBoard2D,
            names[camera], context);
        if (!resolved) return std::unexpected(resolved.error());

        constexpr std::uint64_t MaximumFileBytes = 4U * 1024U * 1024U;
        std::ifstream input(*resolved, std::ios::binary | std::ios::ate);
        if (!input) return std::unexpected("cannot open custom 2D board bitmap");
        const auto size = input.tellg();
        if (size < 0 || static_cast<std::uint64_t>(size) > MaximumFileBytes)
            return std::unexpected("custom 2D board bitmap exceeds file budget");
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        input.seekg(0, std::ios::beg);
        if (!bytes.empty())
            input.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        if (!input) return std::unexpected("cannot read complete custom 2D board bitmap");
        const auto decoded = data::decodeLegacyBitmapRGBA8(
            bytes, static_cast<std::size_t>(MainBoardWidth) * MainBoardHeight);
        if (!decoded) return std::unexpected(decoded.error().detail);
        return *decoded;
    }

    std::expected<void, std::string> BoardBackdropPlayback::validateCustomBoardSet(
        const std::filesystem::path& customRoot,
        engine::SequencePlayback& playback) const
    {
        if (customRoot.empty() || !customRoot.is_absolute())
            return std::unexpected("custom UDBoard backdrop requires an absolute asset root");
        const auto resources = playback.resources();
        if (!resources) return std::unexpected("custom UDBoard backdrop has no resource snapshot");
        const data::BoardTextureContext context{
            resources->context().board, resources->context().language,
            -1, 0, customRoot};
        std::error_code error;
        for (const auto name : data::twoDimensionalBoardTextureNames())
        {
            const auto resolved = data::resolveBoardTexturePath(resources->paths(),
                data::BoardMeshKind::CityMedium, data::TextureLocation::CustomBoard2D,
                name, context);
            if (!resolved) return std::unexpected(resolved.error());
            if (!std::filesystem::is_regular_file(*resolved, error) || error)
                return std::unexpected("custom 2D board set is incomplete");
        }
        return {};
    }

    std::expected<void, std::string> BoardBackdropPlayback::compileImageInto(
        data::DataId surface, const data::LegacyBitmapRGBA8& sourceImage,
        engine::SequencePlayback& playback) const
    {
        const auto target = playback.runtimeBitmaps().asset(surface);
        if (!target)
            return std::unexpected("UDBoard backdrop runtime surface is missing");
        auto composed = opaqueBlack(target->image.width, target->image.height);
        const auto copied = data::blitStraightRGBA8(
            composed, sourceImage, 0, 0, data::BitmapBlitMode::Replace);
        if (!copied) return std::unexpected(copied.error());
        return playback.runtimeBitmaps().update(surface, std::move(composed));
    }

    std::expected<void, std::string> BoardBackdropPlayback::compileInto(
        data::DataId surface, data::DataId source,
        engine::SequencePlayback& playback) const
    {
        const auto sourceImage = loadBoardBitmap(source, playback);
        if (!sourceImage) return std::unexpected(sourceImage.error());
        return compileImageInto(surface, *sourceImage, playback);
    }

    std::expected<data::DataId, std::string> BoardBackdropPlayback::selectBackdrop(
        display::Screen2D view, int city, pieces::BoardCameraView camera,
        std::uint32_t tick, const std::filesystem::path& customRoot,
        engine::SequencePlayback& playback)
    {
        const auto index = cameraIndex(camera);
        if (!index) return std::unexpected(index.error());
        const bool custom = city == -1;
        std::optional<std::uint32_t> cityValue;
        if (!custom)
        {
            const auto stockCity = cityIndex(city);
            if (!stockCity) return std::unexpected(stockCity.error());
            cityValue = *stockCity;
        }

        if (view == display::Screen2D::Main)
        {
            std::size_t oldestIndex = 0;
            std::uint32_t oldest = mainBuffers_[0].timeLoaded;
            std::optional<std::size_t> found;
            for (std::size_t i = 0; i < mainBuffers_.size(); ++i)
            {
                if (oldest > mainBuffers_[i].timeLoaded)
                {
                    oldest = mainBuffers_[i].timeLoaded;
                    oldestIndex = i;
                }
                if (mainBuffers_[i].cityLoaded == city &&
                    mainBuffers_[i].viewLoaded == static_cast<int>(*index) &&
                    (!custom || mainBuffers_[i].customRootLoaded == customRoot))
                    found = i;
            }

            if (found)
            {
                currentMainBuffer_ = *found;
                return mainBuffers_[*found].surface;
            }

            std::expected<void, std::string> compiled;
            if (custom)
            {
                if (validatedCustomRoot_ != customRoot)
                {
                    const auto validated = validateCustomBoardSet(customRoot, playback);
                    if (!validated) return std::unexpected(validated.error());
                    validatedCustomRoot_ = customRoot;
                }
                const auto sourceImage = loadCustomBoardBitmap(*index, customRoot, playback);
                if (!sourceImage) return std::unexpected(sourceImage.error());
                compiled = compileImageInto(
                    mainBuffers_[oldestIndex].surface, *sourceImage, playback);
            }
            else
            {
                const auto source = boardBitmapId(
                    MainBoardBitmapBaseTag, *cityValue, *index);
                compiled = compileInto(
                    mainBuffers_[oldestIndex].surface, source, playback);
            }
            if (!compiled) return std::unexpected(compiled.error());
            mainBuffers_[oldestIndex].cityLoaded = city;
            mainBuffers_[oldestIndex].viewLoaded = static_cast<int>(*index);
            mainBuffers_[oldestIndex].timeLoaded = tick;
            mainBuffers_[oldestIndex].customRootLoaded = custom
                ? customRoot : std::filesystem::path{};
            currentMainBuffer_ = oldestIndex;
            return mainBuffers_[oldestIndex].surface;
        }

        if (view == display::Screen2D::Portfolio ||
            view == display::Screen2D::Trade)
        {
            // Legacy custom boards replace the full-size Main camera set only.
            // Trade/Portfolio continue to use the stock classic-small board.
            const auto source = boardBitmapId(
                TradeBoardBitmapBaseTag, cityValue.value_or(0U), *index);
            const auto compiled = compileInto(tradeSurface_, source, playback);
            if (!compiled) return std::unexpected(compiled.error());
            return tradeSurface_;
        }

        return std::unexpected("UDBoard backdrop requested for non-board view");
    }

    std::expected<void, std::string> BoardBackdropPlayback::sync(
        const BoardBackdropInputs& inputs, engine::SequencePlayback& playback)
    {
        const bool shouldRun = display::isBoardVisible(inputs.view) && !inputs.game3DOn;
        if (!shouldRun)
        {
            if (activeBackdrop_ == data::EmptyDataId) return {};
            if (playback.commands().pendingCount() >=
                sequence::SequenceCommandQueue::Capacity)
                return std::unexpected("sequence command queue capacity exceeded");
            const auto stopped = playback.stop(activeBackdrop_, BoardBackdropPriority);
            if (!stopped) return stopped;
            activeBackdrop_ = data::EmptyDataId;
            currentView_ = display::Screen2D::Invalid;
            currentCity_.reset();
            currentCamera_.reset();
            currentCustomRoot_.clear();
            return {};
        }

        const bool custom = inputs.city == -1;
        if (custom)
        {
            if (inputs.customRoot.empty() || !inputs.customRoot.is_absolute())
                return std::unexpected("custom UDBoard backdrop requires an absolute asset root");
            if (inputs.view == display::Screen2D::Main &&
                validatedCustomRoot_ != inputs.customRoot)
            {
                const auto validated = validateCustomBoardSet(inputs.customRoot, playback);
                if (!validated) return std::unexpected(validated.error());
                validatedCustomRoot_ = inputs.customRoot;
            }
        }
        else
        {
            const auto cityValue = cityIndex(inputs.city);
            if (!cityValue) return std::unexpected(cityValue.error());
        }
        const auto requestedCustomRoot = custom
            ? inputs.customRoot : std::filesystem::path{};

        if (activeBackdrop_ != data::EmptyDataId &&
            currentView_ == inputs.view &&
            currentCity_ == inputs.city &&
            currentCamera_ == inputs.camera &&
            currentCustomRoot_ == requestedCustomRoot)
            return {};

        const std::size_t required =
            activeBackdrop_ == data::EmptyDataId ? 2U : 3U;
        if (playback.commands().pendingCount() >
            sequence::SequenceCommandQueue::Capacity - required)
            return std::unexpected("sequence command queue capacity exceeded");

        const auto surfaces = ensureSurfaces(playback);
        if (!surfaces) return surfaces;
        const auto selected = selectBackdrop(
            inputs.view, inputs.city, inputs.camera, inputs.tick,
            requestedCustomRoot, playback);
        if (!selected) return std::unexpected(selected.error());

        const auto [x, y] = backdropPosition(inputs.view);
        const std::optional<data::DataId> previous =
            activeBackdrop_ == data::EmptyDataId
                ? std::nullopt : std::optional<data::DataId>{activeBackdrop_};
        const auto transitioned = playback.transitionXY(
            previous, *selected, BoardBackdropPriority, x, y, false);
        if (!transitioned) return transitioned;

        activeBackdrop_ = *selected;
        currentView_ = inputs.view;
        currentCity_ = inputs.city;
        currentCamera_ = inputs.camera;
        currentCustomRoot_ = requestedCustomRoot;
        return {};
    }

    void BoardBackdropPlayback::reset() noexcept
    {
        mainBuffers_ = {};
        tradeSurface_ = data::EmptyDataId;
        currentMainBuffer_.reset();
        activeBackdrop_ = data::EmptyDataId;
        currentView_ = display::Screen2D::Invalid;
        currentCity_.reset();
        currentCamera_.reset();
        currentCustomRoot_.clear();
        validatedCustomRoot_.clear();
        surfacesReady_ = false;
    }
}
