// Offline CPU render-intent extractor. Redirect stdout into a build directory.
// No DAT/CNK/HMD payloads are copied; playback uses the production decoder/runtime.
#include "ModernTokenCatalog.hpp"
#include "ResourcePaths.hpp"
#include "ResourceRuntime.hpp"
#include "SequenceRuntime.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <set>
#include <string_view>
#include <system_error>

namespace
{
    using namespace monopoly;

    bool number(std::string_view text, std::uint32_t& value)
    {
        int base = 10;
        if (text.starts_with("0x") || text.starts_with("0X"))
        { text.remove_prefix(2); base = 16; }
        if (text.empty()) return false;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }

    void quoted(std::string_view text)
    {
        constexpr char hex[] = "0123456789abcdef";
        std::cout << '"';
        for (const unsigned char ch : text)
        {
            if (ch == '"' || ch == '\\') std::cout << '\\' << static_cast<char>(ch);
            else if (ch < 0x20)
                std::cout << "\\u00" << hex[ch >> 4] << hex[ch & 15];
            else std::cout << static_cast<char>(ch);
        }
        std::cout << '"';
    }

    void real(float value)
    {
        // Malformed/non-finite matrices must never produce invalid JSON.
        if (std::isfinite(value)) std::cout << value;
        else std::cout << "null";
    }

    template<std::size_t Size>
    void matrix(const std::array<float, Size>& values)
    {
        std::cout << '[';
        for (std::size_t index = 0; index < Size; ++index)
        {
            if (index) std::cout << ',';
            real(values[index]);
        }
        std::cout << ']';
    }

    void transform(const sequence::SequenceTransform& value)
    {
        if (const auto* pose = std::get_if<sequence::Matrix3D>(&value)) matrix(pose->values);
        else if (const auto* pose2D = std::get_if<sequence::Matrix2D>(&value)) matrix(pose2D->values);
        else std::cout << "null";
    }

    void events(std::span<const sequence::SequenceEvent> trace, std::int32_t tick)
    {
        for (const auto& event : trace)
        {
            if (event.kind == sequence::SequenceEventKind::Updated) continue;
            const auto name = [&]() -> std::string_view
            {
                switch (event.kind)
                {
                case sequence::SequenceEventKind::Created: return "created";
                case sequence::SequenceEventKind::ReachedEnd: return "reached_end";
                case sequence::SequenceEventKind::Rewound: return "rewound";
                case sequence::SequenceEventKind::Destroyed: return "destroyed";
                default: return "updated";
                }
            }();
            std::cout << "{\"record\":\"event\",\"tick\":" << tick << ",\"event\":";
            quoted(name);
            std::cout << ",\"node\":" << event.node << ",\"parent\":" << event.parent
                << ",\"data_id\":" << event.dataId << ",\"offset\":" << event.offset
                << ",\"clock\":" << event.clock << ",\"type\":" << unsigned(event.sequenceType)
                << ",\"ending_action\":" << unsigned(event.endingAction) << "}\n";
        }
    }

    void frame(const sequence::SequenceRuntime& runtime, std::int32_t tick,
        sequence::SequenceNodeId root)
    {
        const auto rootView = runtime.inspect(root);
        std::cout << "{\"record\":\"frame\",\"tick\":" << tick
            << ",\"root_present\":" << (rootView ? "true" : "false") << ",\"root_clock\":";
        if (rootView) std::cout << rootView->clock; else std::cout << "null";
        std::cout << ",\"root_end_time\":";
        if (rootView) std::cout << rootView->endTime; else std::cout << "null";
        std::cout << ",\"meshes\":[";
        bool first = true;
        for (const auto& mesh : runtime.meshInstances())
        {
            const auto token = data::tokenForLegacyMesh(mesh.contentsDataId);
            if (!token) continue;
            if (!first) std::cout << ',';
            first = false;
            const auto node = runtime.inspect(mesh.node);
            std::cout << "{\"token\":" << unsigned(*token) << ",\"node\":" << mesh.node
                << ",\"root_sequence_data_id\":" << mesh.rootSequenceDataId
                << ",\"contents_data_id\":" << mesh.contentsDataId << ",\"clock\":" << mesh.clock
                << ",\"priority\":" << mesh.priority << ",\"mesh_a\":" << mesh.meshChoice.meshIndexA
                << ",\"mesh_b\":" << mesh.meshChoice.meshIndexB << ",\"mesh_proportion\":";
            real(mesh.meshChoice.meshProportion);
            std::cout << ",\"world\":";
            matrix(mesh.worldTransform.values);
            if (node)
            {
                std::cout << ",\"parent\":" << node->parent << ",\"data_id\":" << node->dataId
                    << ",\"offset\":" << node->offset << ",\"end_time\":" << node->endTime
                    << ",\"time_multiple\":" << unsigned(node->timeMultiple) << ",\"local\":";
                transform(node->localTransform);
                std::cout << ",\"tweeker_applied\":" << (node->tweekerTransformApplied ? "true" : "false")
                    << ",\"tweeker\":";
                transform(node->tweekerTransform);
            }
            std::cout << '}';
        }
        std::cout << "]}\n";
    }

    int run(int argc, char** argv)
    {
        const bool summaryOnly = argc > 1 && std::string_view(argv[argc - 1]) == "--summary";
        if (summaryOnly) --argc;
        std::uint32_t id{}, duration{}, step{1}, priority{}, endingAction{};
        constexpr std::uint32_t MaximumTicks = 36'000;
        if (argc < 4 || argc > 8 || !number(argv[2], id) || !number(argv[3], duration)
            || duration > MaximumTicks || (argc >= 5 && !number(argv[4], step))
            || step == 0 || step > MaximumTicks
            || (argc >= 7 && (!number(argv[6], priority) || priority > 65535))
            || (argc >= 8 && (!number(argv[7], endingAction) || endingAction > 3)))
        {
            std::cerr << "usage: MonopolyTokenAnimationTimeline <retail-root> <sequence-data-id> "
                "<max-ticks:0..36000> [sample-step:1..36000] [context] "
                "[priority:0..65535] [ending-action:0..3] [--summary]\n"
                "IDs accept decimal or 0x hexadecimal; ticks use the 60 Hz ArtLib parent clock.\n"
                "JSONL goes to stdout; redirect it into a build directory.\n";
            return 2;
        }
        const std::array roots{std::filesystem::absolute(argv[1])};
        const auto paths = data::ResourcePaths::create(roots);
        if (!paths) { std::cerr << paths.error().detail << '\n'; return 1; }
        data::ResourceRuntime resources;
        const auto initialized = resources.initialize(*paths);
        if (!initialized) { std::cerr << initialized.error().detail << '\n'; return 1; }
        const auto snapshot = resources.snapshot();
        const auto program = sequence::SequenceProgram::load(snapshot, id, 0, {32, 1024, 8192});
        if (!program) { std::cerr << program.error().detail << '\n'; return 1; }
        sequence::SequenceRuntime runtime({4096, 8192});
        sequence::ClockStartOptions options;
        // Production transitions enable dropped frames, then override the
        // top-level ending action. Zero requests untouched disk semantics.
        if (endingAction != 0) options.dropFrames = true;
        const auto root = runtime.start(*program, static_cast<std::uint16_t>(priority), options);
        if (!root) { std::cerr << root.error().detail << '\n'; return 1; }
        const std::vector<sequence::SequenceEvent> startEvents(runtime.events().begin(), runtime.events().end());
        if (endingAction != 0)
        {
            const auto changed = runtime.setEndingAction(*root, static_cast<std::uint8_t>(endingAction));
            if (!changed) { std::cerr << changed.error().detail << '\n'; return 1; }
        }
        const auto initialRoot = runtime.inspect(*root);
        std::set<data::DataId> referencedMeshes, observedMeshes;
        std::optional<std::int32_t> firstRootEndTick;
        std::size_t rootEndCount{};

        std::cout.imbue(std::locale::classic());
        std::cout << std::setprecision(std::numeric_limits<float>::max_digits10);
        if (!summaryOnly)
        {
        std::cout << "{\"record\":\"metadata\",\"schema\":1,\"sequence_data_id\":" << id
            << ",\"max_ticks\":" << duration << ",\"sample_step\":" << step
            << ",\"ticks_per_second\":60,\"context\":";
        quoted(argc >= 6 ? argv[5] : "standalone_identity_root");
        std::cout << ",\"priority\":" << priority << ",\"ending_action_override\":" << endingAction;
        std::cout << ",\"board\":" << unsigned(snapshot->context().board)
            << ",\"language\":" << unsigned(snapshot->context().language)
            << ",\"matrix_convention\":\"ArtLib row-vector\","
               "\"media_clocks\":\"unsupplied\",\"root_transform\":\"decoded_default\"}\n";
        }
        for (const auto& description : (*program)->descriptions())
        {
            if (description.contentsDataId && data::tokenForLegacyMesh(*description.contentsDataId))
                referencedMeshes.insert(*description.contentsDataId);
            if (summaryOnly) continue;
            const auto& header = description.record.header;
            std::cout << "{\"record\":\"description\",\"data_id\":" << description.dataId
                << ",\"offset\":" << description.record.chunk.headerOffset
                << ",\"type\":" << unsigned(description.record.chunk.id)
                << ",\"parent_start_time\":" << header.parentStartTime
                << ",\"disk_end_time\":" << header.endTime
                << ",\"disk_time_multiple\":" << unsigned(header.timeMultiple)
                << ",\"ending_action\":" << unsigned(header.endingAction)
                << ",\"contents_data_id\":";
            if (description.contentsDataId) std::cout << *description.contentsDataId;
            else std::cout << "null";
            std::cout << "}\n";
        }
        if (!summaryOnly) events(startEvents, 0);
        for (std::uint32_t tick = 0; tick <= duration; ++tick)
        {
            const auto updated = runtime.update(static_cast<std::int32_t>(tick));
            if (!updated) { std::cerr << "tick " << tick << ": " << updated.error().detail << '\n'; return 1; }
            for (const auto& event : runtime.events())
                if (event.node == *root && event.kind == sequence::SequenceEventKind::ReachedEnd)
                {
                    if (!firstRootEndTick) firstRootEndTick = static_cast<std::int32_t>(tick);
                    ++rootEndCount;
                }
            for (const auto& mesh : runtime.meshInstances())
                if (data::tokenForLegacyMesh(mesh.contentsDataId)) observedMeshes.insert(mesh.contentsDataId);
            if (!summaryOnly) events(runtime.events(), static_cast<std::int32_t>(tick));
            if (!summaryOnly && (tick % step == 0 || tick == duration))
                frame(runtime, static_cast<std::int32_t>(tick), *root);
        }
        std::cout << "{\"record\":\"summary\",\"schema\":1,\"sequence_data_id\":" << id
            << ",\"max_ticks\":" << duration << ",\"priority\":" << priority
            << ",\"ending_action_override\":" << endingAction << ",\"completed_window\":true,\"sequence_finished\":"
            << (runtime.isSequenceFinished(id, static_cast<std::uint16_t>(priority)) ? "true" : "false")
            << ",\"live_nodes\":" << runtime.liveNodeCount()
            << ",\"description_count\":" << (*program)->descriptions().size()
            << ",\"disk_end_time\":" << (*program)->descriptions().front().record.header.endTime
            << ",\"effective_end_time\":";
        if (initialRoot) std::cout << initialRoot->endTime; else std::cout << "null";
        std::cout << ",\"effective_time_multiple\":";
        if (initialRoot) std::cout << unsigned(initialRoot->timeMultiple); else std::cout << "null";
        const auto finalRoot = runtime.inspect(*root);
        std::cout << ",\"root_present\":" << (finalRoot ? "true" : "false") << ",\"final_root_clock\":";
        if (finalRoot) std::cout << finalRoot->clock; else std::cout << "null";
        std::cout << ",\"first_root_end_tick\":";
        if (firstRootEndTick) std::cout << *firstRootEndTick; else std::cout << "null";
        std::cout << ",\"root_end_count\":" << rootEndCount << ",\"referenced_hmds\":[";
        bool first = true;
        for (const auto mesh : referencedMeshes) { if (!first) std::cout << ','; first = false; std::cout << mesh; }
        std::cout << "],\"observed_hmds\":[";
        first = true;
        for (const auto mesh : observedMeshes) { if (!first) std::cout << ','; first = false; std::cout << mesh; }
        std::cout << "]}\n";
        return std::cout ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    try { return run(argc, argv); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
