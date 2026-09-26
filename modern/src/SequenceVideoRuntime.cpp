#include "SequenceVideoRuntime.hpp"

#include "Timers.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <utility>

namespace monopoly::video
{
    namespace
    {
        constexpr std::uintmax_t MaximumVideoBytes =
            1024ULL * 1024ULL * 1024ULL;

        [[nodiscard]] std::expected<std::filesystem::path, std::string>
        resolveVideoPath(
            const data::ResourcePaths& paths,
            std::string_view legacyName)
        {
            if (legacyName.empty())
                return std::unexpected(
                    "video sequence has no external filename");

            auto direct = paths.resolve(legacyName);
            if (direct) return *direct;
            std::string aviName{"AVI/"};
            aviName.append(legacyName);
            auto avi = paths.resolve(aviName);
            if (avi) return *avi;

            return std::unexpected(
                "video file cannot be resolved from resource roots: " +
                std::string(legacyName));
        }

        [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string>
        readVideoBytes(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error)
                return std::unexpected(
                    "cannot inspect video file: " + error.message());
            if (size == 0 || size > MaximumVideoBytes ||
                size > std::numeric_limits<std::size_t>::max())
                return std::unexpected(
                    "video file size is outside runtime limits");

            std::ifstream stream(path, std::ios::binary);
            if (!stream)
                return std::unexpected("cannot open video file");
            std::vector<std::uint8_t> bytes(
                static_cast<std::size_t>(size));
            stream.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            if (!stream ||
                static_cast<std::size_t>(stream.gcount()) != bytes.size())
                return std::unexpected("cannot read complete video file");
            return bytes;
        }

        [[nodiscard]] std::expected<std::uint64_t, std::string>
        sequenceMicroseconds(std::int32_t clock)
        {
            if (clock <= 0) return 0;
            constexpr auto million = std::uint64_t{1'000'000};
            const auto ticks = static_cast<std::uint64_t>(clock);
            if (ticks >
                std::numeric_limits<std::uint64_t>::max() / million)
                return std::unexpected(
                    "video sequence clock exceeds timeline range");
            return ticks * million / timers::BasicClockRateHz;
        }

        [[nodiscard]] std::expected<
            data::Sequence2DBoundingBoxAttribute, std::string>
        effectiveBoundingBox(
            data::SequenceVideoData& options,
            const AviMetadata& metadata,
            const std::optional<data::Sequence2DBoundingBoxAttribute>& requested)
        {
            if (options.enableVideo &&
                options.doubleAlternateLines &&
                !options.drawDirectlyToScreen)
                options.doubleAlternateLines = false;

            if (requested)
            {
                auto result = *requested;
                if (options.enableVideo && options.doubleAlternateLines)
                {
                    const auto width =
                        static_cast<std::int64_t>(result.right) - result.left;
                    const auto height =
                        static_cast<std::int64_t>(result.bottom) - result.top;
                    if (width != static_cast<std::int64_t>(metadata.width) * 2 ||
                        height != static_cast<std::int64_t>(metadata.height) * 2)
                        options.doubleAlternateLines = false;
                }
                return result;
            }

            data::Sequence2DBoundingBoxAttribute result{};
            if (!options.enableVideo)
            {
                result.right = 1;
                result.bottom = 1;
                return result;
            }

            const std::uint64_t scale =
                options.doubleAlternateLines ? 2U : 1U;
            const auto width =
                static_cast<std::uint64_t>(metadata.width) * scale;
            const auto height =
                static_cast<std::uint64_t>(metadata.height) * scale;
            if (width >
                    static_cast<std::uint64_t>(
                        std::numeric_limits<std::int32_t>::max()) ||
                height >
                    static_cast<std::uint64_t>(
                        std::numeric_limits<std::int32_t>::max()))
                return std::unexpected(
                    "video bounding box exceeds 2D runtime range");

            result.right = static_cast<std::int32_t>(width);
            result.bottom = static_cast<std::int32_t>(height);
            return result;
        }
    }
    std::expected<SequenceRuntimeBridge::Active*, std::string>
    SequenceRuntimeBridge::active(sequence::SequenceNodeId node)
    {
        const auto found = active_.find(node);
        if (found == active_.end())
            return std::unexpected(
                "video command references an inactive sequence");
        return &found->second;
    }

    std::expected<std::vector<SequenceJumpNotice>, std::string>
    SequenceRuntimeBridge::sync(
        const sequence::SequenceRuntime& sequences,
        const data::ResourceSnapshot& resources)
    {
        const auto intents = sequences.videoInstances();
        std::set<sequence::SequenceNodeId> live;
        std::vector<SequenceJumpNotice> jumps;
        states_.clear();
        states_.reserve(intents.size());

        for (const auto& intent : intents)
        {
            live.insert(intent.node);
            auto found = active_.find(intent.node);
            if (found == active_.end())
            {
                auto file = resolveVideoPath(
                    resources.paths(), intent.fileName);
                if (!file) return std::unexpected(file.error());

                auto bytes = readVideoBytes(*file);
                if (!bytes) return std::unexpected(bytes.error());

                Active created{};
                created.file = *file;
                created.options = intent.video;
                const auto opened = created.runtime.open(*bytes, false);
                if (!opened) return std::unexpected(opened.error());
                const auto bounds = effectiveBoundingBox(created.options,
                    created.runtime.metadata(), intent.boundingBox);
                if (!bounds) return std::unexpected(bounds.error());
                created.boundingBox = *bounds;

                found = active_.emplace(
                    intent.node, std::move(created)).first;
            }

            auto& entry = found->second;
            if (entry.file.empty())
                return std::unexpected(
                    "active video sequence lost its resolved file");

            if (!intent.video.enableAudio)
            {
                const auto elapsed = sequenceMicroseconds(intent.elapsedParentClock);
                if (!elapsed) return std::unexpected(elapsed.error());
                const auto desired =
                    entry.runtime.frameAtElapsed(*elapsed);
                if (!desired) return std::unexpected(desired.error());
                if (entry.lastSequenceClock !=
                        std::numeric_limits<std::int32_t>::min() &&
                    intent.elapsedParentClock < entry.lastSequenceClock &&
                    *desired < entry.runtime.status().numberOfFrames)
                {
                    const auto cut = entry.runtime.cutToFrame(*desired);
                    if (!cut) return std::unexpected(cut.error());
                }
                else
                {
                    const auto fed = entry.runtime.feedToFrame(*desired);
                    if (!fed) return std::unexpected(fed.error());
                }
            }

            entry.lastSequenceClock = intent.elapsedParentClock;
            if (auto jump = entry.runtime.takeJumpEvent())
                jumps.push_back({intent.node, *jump});

            states_.push_back({
                intent.node,
                entry.file,
                entry.runtime.status(),
                entry.options,
                entry.boundingBox,
                intent.worldTransform,
                intent.video.enableAudio, true});
        }
        for (auto it = active_.begin(); it != active_.end();)
        {
            if (!live.contains(it->first))
                it = active_.erase(it);
            else
                ++it;
        }

        return jumps;
    }

    std::expected<std::vector<SequenceJumpNotice>, std::string>
    SequenceRuntimeBridge::sync(engine::SequencePlayback& playback)
    {
        const auto resources = playback.resources();
        if (!resources) return std::unexpected("video presentation has no resource snapshot");
        const auto intents = playback.runtime().videoInstances();
        std::set<sequence::SequenceNodeId> live;
        for (const auto& intent : intents) live.insert(intent.node);
        // Stop/remove resources before decoder destruction; no worker sees DATA/SDL GPU state.
        for (auto it = active_.begin(); it != active_.end();)
        {
            if (live.contains(it->first)) { ++it; continue; }
            auto& entry = it->second;
            if (entry.surfaceVisible && entry.surface)
            {
                const auto stopped = playback.stop(*entry.surface, entry.publishedPriority);
                if (!stopped) return std::unexpected(stopped.error());
            }
            if (entry.surface) (void)playback.runtimeBitmaps().remove(*entry.surface);
            it = active_.erase(it);
        }
        states_.clear();
        states_.reserve(intents.size());
        std::vector<SequenceJumpNotice> jumps;
        for (const auto& intent : intents)
        {
            auto found = active_.find(intent.node);
            if (found == active_.end())
            {
                const auto file = resolveVideoPath(resources->paths(), intent.fileName);
                if (!file) return std::unexpected(file.error());
                Active created;
                created.file = *file;
                created.options = intent.video;
                created.loop = intent.endingAction == 3;
                created.presentation = std::make_shared<Presentation>();
                auto decoderOptions = decoderOptions_;
                decoderOptions.decodeVideo = intent.video.enableVideo;
                const auto opened = created.presentation->open(*file,
                    intent.video.enableAudio, decoderOptions);
                if (!opened) return std::unexpected(opened.error());
                found = active_.emplace(intent.node, std::move(created)).first;
            }
            auto& entry = found->second;
            entry.loop = intent.endingAction == 3;
            if (!entry.presentation)
                return std::unexpected("cannot mix metadata-only and decoded video bridge sessions");
            auto snapshot = entry.presentation->snapshot();
            if (snapshot.phase == DecoderPhase::Failed)
                return std::unexpected(snapshot.error);
            if (!snapshot.metadata)
            {
                states_.push_back({intent.node, entry.file, {}, entry.options,
                    intent.boundingBox, intent.worldTransform, intent.video.enableAudio});
                continue;
            }
            const auto& metadata = *snapshot.metadata;
            if (!entry.runtime.open())
            {
                if (!metadata.frameRateNumerator || !metadata.frameRateDenominator ||
                    !metadata.durationMicroseconds)
                    return std::unexpected("decoded video has invalid duration or frame cadence");
                const auto duration = (1000000ULL * metadata.frameRateDenominator +
                    metadata.frameRateNumerator / 2U) / metadata.frameRateNumerator;
                const long double frames = std::ceil(
                    static_cast<long double>(metadata.durationMicroseconds) *
                    metadata.frameRateNumerator / (1000000.0L * metadata.frameRateDenominator));
                if (!duration || duration > std::numeric_limits<std::uint32_t>::max() ||
                    frames < 1 || frames > std::numeric_limits<std::int32_t>::max())
                    return std::unexpected("decoded video timeline exceeds runtime range");
                const auto opened = entry.runtime.open(AviMetadata{
                    static_cast<std::uint32_t>(duration), static_cast<std::uint32_t>(frames),
                    metadata.width, metadata.height});
                if (!opened) return std::unexpected(opened.error());
                const auto bounds = effectiveBoundingBox(entry.options,
                    entry.runtime.metadata(), intent.boundingBox);
                if (!bounds) return std::unexpected(bounds.error());
                entry.boundingBox = intent.binkDoubleSize ?
                    binkDoubleSizeBounds(metadata.width, metadata.height) : *bounds;
                const auto durationTicks = std::ceil(static_cast<long double>(
                    metadata.durationMicroseconds) * timers::BasicClockRateHz / 1000000.0L);
                if (durationTicks < 1 || durationTicks > std::numeric_limits<std::int32_t>::max())
                    return std::unexpected("decoded video duration exceeds sequence clock range");
                entry.durationTicks = static_cast<std::int32_t>(durationTicks);
                entry.clipDurationMicroseconds = metadata.durationMicroseconds;
                if (const auto node = playback.runtime().inspect(intent.node))
                {
                    const auto authoredDuration = sequenceMicroseconds(node->endTime);
                    if (!authoredDuration) return std::unexpected(authoredDuration.error());
                    entry.authoredClip = *authoredDuration < metadata.durationMicroseconds;
                    entry.clipDurationMicroseconds = std::min(
                        entry.clipDurationMicroseconds, *authoredDuration);
                }
            }
            const auto sequenceTime = sequenceMicroseconds(intent.elapsedParentClock);
            if (!sequenceTime) return std::unexpected(sequenceTime.error());
            if (entry.lastSequenceClock != std::numeric_limits<std::int32_t>::min() &&
                intent.elapsedParentClock < entry.lastSequenceClock && !entry.seekPending)
            {
                const auto frame = entry.runtime.frameAtElapsed(*sequenceTime);
                if (!frame) return std::unexpected(frame.error());
                const auto cut = entry.runtime.cutToFrame(
                    std::min(*frame, entry.runtime.status().numberOfFrames - 1));
                if (!cut) return std::unexpected(cut.error());
                entry.requestedSeek = static_cast<std::uint64_t>(entry.runtime.status().currentFrame) *
                    entry.runtime.metadata().microsecondsPerFrame;
                entry.seekPending = true;
            }
            if (entry.seekPending)
            {
                const auto seeked = entry.presentation->seek(entry.requestedSeek, *sequenceTime);
                if (!seeked) return std::unexpected(seeked.error());
                entry.seekPending = false;
            }
            bool paused{};
            for (auto node = playback.runtime().inspect(intent.node); node;
                node = node->parent ? playback.runtime().inspect(node->parent) : std::nullopt)
                paused = paused || node->paused;
            const auto gain = entry.presentation->setGain(
                static_cast<float>(intent.volume) / 100.0F);
            if (!gain) return std::unexpected(gain.error());
            const auto clock = entry.presentation->pump(
                *sequenceTime, paused || entry.runtime.status().ended);
            if (!clock) return std::unexpected(clock.error());
            const auto desired = entry.runtime.frameAtElapsed(std::min(clock->elapsedMicroseconds,
                entry.clipDurationMicroseconds > 0 ? entry.clipDurationMicroseconds - 1 : 0));
            if (!desired) return std::unexpected(desired.error());
            if (!entry.runtime.status().ended)
            {
                const auto fed = entry.runtime.feedToFrame(
                    std::min(*desired, entry.runtime.status().numberOfFrames - 1));
                if (!fed) return std::unexpected(fed.error());
            }
            bool jumped{};
            if (auto event = entry.runtime.takeJumpEvent())
            {
                jumps.push_back({intent.node, *event});
                if (event->alternativeTaken)
                {
                    const auto timestamp = static_cast<std::uint64_t>(event->jumpToFrame) *
                        entry.runtime.metadata().microsecondsPerFrame;
                    const auto seeked = entry.presentation->seek(timestamp, *sequenceTime);
                    if (!seeked) return std::unexpected(seeked.error());
                    jumped = true;
                }
            }
            const auto frameTime = jumped ?
                static_cast<std::uint64_t>(entry.runtime.status().currentFrame) *
                    entry.runtime.metadata().microsecondsPerFrame : std::min(clock->elapsedMicroseconds,
                    entry.clipDurationMicroseconds > 0 ? entry.clipDurationMicroseconds - 1 : 0);
            const auto decisionLimit = static_cast<std::uint64_t>(
                std::max(entry.runtime.status().currentFrame, 0)) + 1U;
            const auto frameEndTime = decisionLimit *
                entry.runtime.metadata().microsecondsPerFrame - 1;
            auto frame = entry.presentation->frameAt(std::min(frameTime, frameEndTime));
            if (!frame) return std::unexpected(frame.error());
            if (entry.options.enableVideo)
            {
                const auto transform = videoFrameTransform(metadata.width, metadata.height,
                    entry.boundingBox, intent.worldTransform);
                if (!transform) return std::unexpected(transform.error());
                const bool moved = entry.surfaceVisible &&
                    (entry.publishedTransform.values != transform->values ||
                     entry.publishedPriority != intent.priority);
                const std::size_t commands = !entry.surfaceVisible && *frame ? 1U :
                    moved ? (entry.publishedPriority != intent.priority ? 2U : 1U) : 0U;
                if (commands > sequence::SequenceCommandQueue::Capacity - playback.commands().pendingCount())
                    return std::unexpected("sequence command queue cannot fit decoded video presentation");
                if (*frame)
                {
                    auto image = prepareVideoFrame(std::move(**frame), entry.options);
                    if (!image) return std::unexpected(image.error());
                    if (!entry.surface)
                    {
                        const auto created = playback.runtimeBitmaps().create(
                            image->width, image->height, !entry.options.drawSolid);
                        if (!created) return std::unexpected(created.error());
                        entry.surface = *created;
                    }
                    const auto updated = playback.runtimeBitmaps().update(*entry.surface, std::move(*image));
                    if (!updated) return std::unexpected(updated.error());
                }
                if (entry.surface && (!entry.surfaceVisible || moved))
                {
                    if (entry.surfaceVisible && entry.publishedPriority != intent.priority)
                    {
                        const auto stopped = playback.stop(*entry.surface, entry.publishedPriority);
                        if (!stopped) return std::unexpected(stopped.error());
                        entry.surfaceVisible = false;
                    }
                    const auto published = entry.surfaceVisible ?
                        playback.move(*entry.surface, intent.priority, *transform) :
                        playback.startMoved(*entry.surface, intent.priority, *transform);
                    if (!published) return std::unexpected(published.error());
                    entry.surfaceVisible = true;
                    entry.publishedTransform = *transform;
                    entry.publishedPriority = intent.priority;
                }
            }
            snapshot = entry.presentation->snapshot();
            const bool decodedEnd = snapshot.phase == DecoderPhase::Ended &&
                entry.presentation->videoDrained() && clock->audioDrained &&
                clock->elapsedMicroseconds >= metadata.durationMicroseconds;
            const bool ended = !jumped && (decodedEnd ||
                (entry.authoredClip && clock->elapsedMicroseconds >= entry.clipDurationMicroseconds));
            const bool finishSequence = ended || entry.runtime.status().ended;
            if (ended && (!entry.runtime.status().ended || entry.loop))
            {
                if (entry.loop)
                {
                    const auto seeked = entry.presentation->seek(0, *sequenceTime);
                    if (!seeked) return std::unexpected(seeked.error());
                    const auto cut = entry.runtime.cutToFrame(0);
                    if (!cut) return std::unexpected(cut.error());
                }
                else
                {
                    const auto fed = entry.runtime.feedToFrame(entry.runtime.status().numberOfFrames);
                    if (!fed) return std::unexpected(fed.error());
                }
            }
            if (finishSequence && !entry.loop)
            {
                const auto pausedAtEnd = entry.presentation->pump(*sequenceTime, true);
                if (!pausedAtEnd) return std::unexpected(pausedAtEnd.error());
                if (intent.endingAction <= 1 && entry.surface)
                {
                    if (entry.surfaceVisible)
                    {
                        const auto stopped = playback.stop(*entry.surface, entry.publishedPriority);
                        if (!stopped) return std::unexpected(stopped.error());
                    }
                    (void)playback.runtimeBitmaps().remove(*entry.surface);
                    entry.surface.reset();
                    entry.surfaceVisible = false;
                }
            }
            const auto mediaTicks = static_cast<std::int32_t>(std::min<long double>(
                static_cast<long double>(clock->elapsedMicroseconds) * timers::BasicClockRateHz / 1000000.0L,
                entry.durationTicks));
            const auto supplied = playback.runtime().requestVideoClock(
                intent.node, mediaTicks, entry.durationTicks, finishSequence);
            if (!supplied) return std::unexpected(supplied.error().detail);
            entry.lastSequenceClock = intent.elapsedParentClock;
            states_.push_back({intent.node, entry.file, entry.runtime.status(), entry.options,
                entry.boundingBox, intent.worldTransform, clock->waitingForAudio,
                true, entry.surface, clock->consumedAudioBytes});
        }
        return jumps;
    }

    std::expected<void, std::string> SequenceRuntimeBridge::reset(
        engine::SequencePlayback& playback)
    {
        const auto stops = static_cast<std::size_t>(std::count_if(active_.begin(), active_.end(),
            [](const auto& entry) { return entry.second.surfaceVisible; }));
        if (stops > sequence::SequenceCommandQueue::Capacity - playback.commands().pendingCount())
            return std::unexpected("sequence command queue cannot fit video reset");
        for (const auto& [node, entry] : active_)
        {
            (void)node;
            if (entry.surface)
            {
                if (entry.surfaceVisible)
                {
                    // Ordered after an unexecuted Start as well as after live roots.
                    const auto stopped = playback.stop(*entry.surface, entry.publishedPriority);
                    if (!stopped) return stopped;
                }
                (void)playback.runtime().stopMatching(*entry.surface, entry.publishedPriority);
                (void)playback.runtimeBitmaps().remove(*entry.surface);
            }
        }
        reset();
        return {};
    }
    std::expected<void, std::string> SequenceRuntimeBridge::setAlternative(
        sequence::SequenceNodeId node,
        std::int32_t decisionFrame,
        std::int32_t jumpToFrame)
    {
        auto entry = active(node);
        if (!entry) return std::unexpected(entry.error());
        return (*entry)->runtime.setAlternative(
            decisionFrame, jumpToFrame);
    }

    std::expected<void, std::string> SequenceRuntimeBridge::chooseAlternative(
        sequence::SequenceNodeId node,
        std::int32_t decisionFrame,
        bool takeAlternative)
    {
        auto entry = active(node);
        if (!entry) return std::unexpected(entry.error());
        if (!(*entry)->runtime.changeAlternative(
                decisionFrame, takeAlternative))
            return std::unexpected(
                "video alternative decision frame is not configured");
        return {};
    }
    std::expected<void, std::string> SequenceRuntimeBridge::forgetAlternatives(
        sequence::SequenceNodeId node)
    {
        auto entry = active(node);
        if (!entry) return std::unexpected(entry.error());
        (*entry)->runtime.forgetAlternatives();
        return {};
    }

    std::expected<void, std::string> SequenceRuntimeBridge::cutToFrame(
        sequence::SequenceNodeId node,
        std::int32_t frame)
    {
        auto entry = active(node);
        if (!entry) return std::unexpected(entry.error());
        const auto cut = (*entry)->runtime.cutToFrame(frame);
        if (!cut) return cut;
        if ((*entry)->presentation)
        {
            (*entry)->requestedSeek = static_cast<std::uint64_t>(frame) *
                (*entry)->runtime.metadata().microsecondsPerFrame;
            (*entry)->seekPending = true;
        }
        return {};
    }

    void SequenceRuntimeBridge::reset() noexcept
    {
        active_.clear();
        states_.clear();
    }
}
