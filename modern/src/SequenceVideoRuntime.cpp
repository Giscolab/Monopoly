#include "SequenceVideoRuntime.hpp"

#include "Timers.hpp"

#include <algorithm>
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

                found = active_.emplace(
                    intent.node, std::move(created)).first;
            }

            auto& entry = found->second;
            if (entry.file.empty())
                return std::unexpected(
                    "active video sequence lost its resolved file");

            if (!intent.video.enableAudio)
            {
                const auto elapsed = sequenceMicroseconds(intent.clock);
                if (!elapsed) return std::unexpected(elapsed.error());
                const auto desired =
                    entry.runtime.frameAtElapsed(*elapsed);
                if (!desired) return std::unexpected(desired.error());
                if (entry.lastSequenceClock !=
                        std::numeric_limits<std::int32_t>::min() &&
                    intent.clock < entry.lastSequenceClock &&
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

            entry.lastSequenceClock = intent.clock;
            if (auto jump = entry.runtime.takeJumpEvent())
                jumps.push_back({intent.node, *jump});

            states_.push_back({
                intent.node,
                entry.file,
                entry.runtime.status(),
                intent.video,
                intent.boundingBox,
                intent.worldTransform,
                intent.video.enableAudio});
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
        return (*entry)->runtime.cutToFrame(frame);
    }

    void SequenceRuntimeBridge::reset() noexcept
    {
        active_.clear();
        states_.clear();
    }
}
