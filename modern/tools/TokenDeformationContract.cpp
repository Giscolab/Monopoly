// Diagnostic referenced-control correspondence, decoded by production DATA/HMD APIs.
#include "LegacyMeshData.hpp"
#include "ResourceRuntime.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace monopoly::data;
    using Position = std::array<int, 3>;
    using Triangle = std::array<std::uint16_t, 3>;
    struct Mesh
    {
        DataId id{};
        std::size_t payloadBytes{}, vertexPool{};
        std::map<std::uint16_t, Position> controls;
        std::vector<Triangle> triangles, topology;
    };
    DataId number(std::string_view text)
    {
        int radix = 10;
        if (text.starts_with("0x") || text.starts_with("0X"))
        { text.remove_prefix(2); radix = 16; }
        DataId value{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, radix);
        if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            throw std::runtime_error("invalid DATA id");
        return value;
    }
    Mesh decode(const DataSource& source, DataId id)
    {
        auto loaded = openLegacyMeshData(source, id);
        if (!loaded) throw std::runtime_error(loaded.error().detail);
        Mesh result; result.id = id; result.payloadBytes = loaded->bytes().size();
        std::optional<std::size_t> pool;
        const auto primitives = loaded->primitives();
        for (std::size_t p = 0; p < primitives.size(); ++p)
            for (std::size_t s = 0; s < primitives[p].sections.size(); ++s)
            {
                const auto& section = primitives[p].sections[s];
                // Images and MIMe metadata are not base geometry controls.
                if (((section.type >> 24) & 15U) != 0 || !section.elementCount) continue;
                for (std::size_t t = 0; t < section.elementCount; ++t)
                {
                    const auto triangle = loaded->triangle(p, s, t);
                    if (!triangle) throw std::runtime_error(triangle.error().detail);
                    const auto& header = loaded->headers()[primitives[p].headerIndex];
                    const auto vertexPool = static_cast<std::size_t>(header.fields[1].rawValue & 0x7FFFFFFFU) * 4;
                    if (pool && *pool != vertexPool)
                        throw std::runtime_error("multiple vertex pools require an explicit correspondence adapter");
                    pool = vertexPool;
                    result.triangles.push_back(triangle->vertexIndices);
                    for (std::size_t corner = 0; corner < 3; ++corner)
                    {
                        const auto v = triangle->vertices[corner];
                        const Position position{v.x, -static_cast<int>(v.y), v.z};
                        const auto [it, inserted] = result.controls.emplace(triangle->vertexIndices[corner], position);
                        if (!inserted && it->second != position)
                            throw std::runtime_error("one source index has inconsistent decoded positions");
                    }
                }
            }
        if (!pool || result.controls.empty()) throw std::runtime_error("no supported referenced controls");
        result.vertexPool = *pool; result.topology = result.triangles;
        for (auto& triangle : result.topology) std::sort(triangle.begin(), triangle.end());
        std::sort(result.topology.begin(), result.topology.end());
        return result;
    }
    void qualify(const Mesh& rest, const Mesh& target)
    {
        if (rest.topology != target.topology || rest.controls.size() != target.controls.size())
            throw std::runtime_error("complete triangle-index multisets or referenced-control counts differ");
        std::map<Position, Position> duplicateTargets;
        for (const auto& [index, position] : rest.controls)
        {
            const auto found = target.controls.find(index);
            if (found == target.controls.end()) throw std::runtime_error("target source index is missing");
            const auto [it, inserted] = duplicateTargets.emplace(position, found->second);
            if (!inserted && it->second != found->second)
                throw std::runtime_error("duplicate rest positions have conflicting target controls");
        }
    }
    bool sourceComponent(const std::filesystem::path& path)
    {
        for (const auto& part : path)
        {
            auto name = part.wstring();
            std::transform(name.begin(), name.end(), name.begin(),
                [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            if (name == L"source") return true;
        }
        return false;
    }
    std::filesystem::path outputPath(std::filesystem::path requested)
    {
        requested = std::filesystem::absolute(requested);
        if (std::filesystem::is_directory(requested)) requested /= "token_deformation_contract.json";
        if (sourceComponent(requested) || requested.extension() != ".json")
            throw std::runtime_error("output must be JSON outside immutable Source");
        auto prefix = requested.root_path();
        for (const auto& part : requested.relative_path())
        {
            prefix /= part;
            std::error_code ec;
            if (std::filesystem::is_symlink(std::filesystem::symlink_status(prefix, ec)))
                throw std::runtime_error("symlink output paths are refused");
        }
        const auto parent = std::filesystem::canonical(requested.parent_path());
        if (sourceComponent(parent)) throw std::runtime_error("resolved output enters immutable Source");
        auto ancestor = parent;
        while (!std::filesystem::is_regular_file(ancestor / "CMakeCache.txt"))
        {
            const auto next = ancestor.parent_path();
            if (next == ancestor || next.empty()) throw std::runtime_error("output requires an existing CMake build directory");
            ancestor = next;
        }
        return parent / requested.filename();
    }
    template<typename Range> void numbers(std::ostream& out, const Range& values)
    {
        out << '['; bool first = true;
        for (const auto value : values) { if (!first) out << ','; first = false; out << value; }
        out << ']';
    }
    void positions(std::ostream& out, const Mesh& mesh)
    {
        out << '['; bool first = true;
        for (const auto& [index, position] : mesh.controls)
        { (void)index; if (!first) out << ','; first = false; numbers(out, position); }
        out << ']';
    }
}

int main(int argc, char** argv)
{
    if (argc != 5)
    { std::cerr << "usage: TokenDeformationContract <retail-root> <baseHMDid> <targetHMDid[,ids...]> <build-dir-or-json-output>\n"; return 2; }
    try
    {
        const auto destination = outputPath(argv[4]);
        ResourceRuntime resources;
        const auto paths = ResourcePaths::create(std::array{std::filesystem::absolute(argv[1])});
        if (!paths) throw std::runtime_error(paths.error().detail);
        const auto initialized = resources.initialize(*paths);
        if (!initialized) throw std::runtime_error(initialized.error().detail);
        const auto snapshot = resources.snapshot();
        const auto rest = decode(snapshot->data(), number(argv[2]));
        std::vector<Mesh> targets;
        std::string_view remaining = argv[3];
        do {
            const auto comma = remaining.find(',');
            auto target = decode(snapshot->data(), number(remaining.substr(0, comma)));
            if (dataGroup(target.id) != dataGroup(rest.id))
                throw std::runtime_error("target and rest must use the same DATA bank");
            qualify(rest, target);
            if (std::any_of(targets.begin(), targets.end(), [&](const auto& m) { return m.id == target.id; }))
                throw std::runtime_error("duplicate target DATA id");
            targets.push_back(std::move(target));
            if (targets.size() > 256) throw std::runtime_error("target budget exceeded");
            if (comma == std::string_view::npos) break;
            remaining.remove_prefix(comma + 1);
        } while (true);
        std::map<std::uint16_t, std::uint16_t> remap;
        for (const auto& [index, position] : rest.controls)
        { (void)position; remap.emplace(index, static_cast<std::uint16_t>(remap.size())); }
        std::ostringstream json;
        json << "{\n\"schema\":2,\"source_indices\":";
        std::vector<std::uint16_t> indices; for (const auto& [index, position] : rest.controls) { (void)position; indices.push_back(index); }
        numbers(json, indices); json << ",\n\"rest_positions_engine\":"; positions(json, rest);
        json << ",\n\"rest_triangles\":[";
        for (std::size_t i = 0; i < rest.triangles.size(); ++i)
        { if (i) json << ','; const auto& t = rest.triangles[i]; numbers(json, Triangle{remap.at(t[0]), remap.at(t[1]), remap.at(t[2])}); }
        json << "],\n\"target_positions_engine\":{";
        for (std::size_t i = 0; i < targets.size(); ++i)
        { if (i) json << ','; json << "\"0x" << std::hex << dataTag(targets[i].id) << std::dec << "\":"; positions(json, targets[i]); }
        json << "},\n\"squash_positions_engine\":"; positions(json, targets.front());
        json << ",\n\"evidence\":{\"identical_triangle_multisets\":true,\"duplicate_rest_positions_have_consistent_targets\":true,"
                "\"referenced_controls_only\":true,\"unreferenced_control_indices\":[],\"coordinate_conversion\":\"engine=(retail.x,-retail.y,retail.z); no calibration or CNK transforms\","
                "\"parser_reference\":\"production LegacyMeshData::triangle\",\"production_geometry_decoder\":true,\"hashes_available\":false,\"rest_data_id\":" << rest.id
             << ",\"control_count\":" << rest.controls.size() << ",\"triangle_count\":" << rest.triangles.size() << ",\"meshes\":[";
        auto provenance = [&](const Mesh& mesh) {
            const auto archive = snapshot->banks().archive(dataGroup(mesh.id));
            const auto item = snapshot->data().metadata(mesh.id);
            json << "{\"data_id\":" << mesh.id << ",\"payload_bytes\":" << mesh.payloadBytes << ",\"vertex_pool_offset\":" << mesh.vertexPool;
            if (item) json << ",\"archive_offset\":" << item->offset << ",\"compressed_bytes\":" << item->compressedSize;
            if (archive)
            {
                const auto utf8 = (*archive)->path().generic_u8string();
                const std::string path(reinterpret_cast<const char*>(utf8.data()), utf8.size());
                if (std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 32; }))
                    throw std::runtime_error("control characters in archive paths are unsupported");
                json << ",\"archive_path\":" << std::quoted(path);
            }
            json << '}';
        };
        provenance(rest); for (const auto& target : targets) { json << ','; provenance(target); }
        json << "],\"targets\":{";
        for (std::size_t i = 0; i < targets.size(); ++i)
        {
            if (i) json << ',';
            json << "\"0x" << std::hex << dataTag(targets[i].id) << std::dec
                 << "\":{\"identical_triangle_multisets\":true,\"duplicate_rest_positions_have_consistent_targets\":true,\"data_id\":"
                 << targets[i].id << '}';
        }
        json << "}}}\n";
        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        output << json.str(); output.close();
        if (!output) throw std::runtime_error("cannot write complete diagnostic JSON");
        std::cout << destination << " controls=" << rest.controls.size() << " triangles=" << rest.triangles.size() << '\n';
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
