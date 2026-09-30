#include "MeshRuntime.hpp"
#include "ModernGltfMesh.hpp"
#include "ModernTokenCatalog.hpp"
#include "ResourcePaths.hpp"
#include "ResourceRuntime.hpp"
#include "SequenceRuntime.hpp"

#include <array>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <set>
#include <span>

namespace
{
    using namespace monopoly::data;

    struct Dimensions
    {
        std::array<float, 3> size{};
        std::array<float, 3> center{};
    };

    [[nodiscard]] Dimensions dimensions(const MeshBounds& bounds) noexcept
    {
        Dimensions result;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            result.size[axis] = bounds.maximum[axis] - bounds.minimum[axis];
            result.center[axis] =
                (bounds.maximum[axis] + bounds.minimum[axis]) * 0.5F;
        }
        return result;
    }

    void printVector(const std::array<float, 3>& value)
    {
        std::cout << std::fixed << std::setprecision(3)
                  << value[0] << ',' << value[1] << ',' << value[2];
    }
}

int main(int argc, char** argv)
{
    using namespace monopoly::data;

    if (argc != 3)
    {
        std::cerr
            << "usage: MonopolyModernAssetProbe <retail-root> <modern-assets-root>\n";
        return 2;
    }

    const std::filesystem::path retailRoot =
        std::filesystem::absolute(argv[1]);
    const std::filesystem::path modernRoot =
        std::filesystem::absolute(argv[2]);

    const std::array roots{retailRoot};
    auto paths = ResourcePaths::create(roots);
    if (!paths)
    {
        std::cerr << "resource root: " << paths.error().detail << '\n';
        return 1;
    }

    ResourceRuntime runtime;
    auto initialized = runtime.initialize(*paths);
    if (!initialized)
    {
        std::cerr << "resource runtime: "
                  << initialized.error().detail << '\n';
        return 1;
    }

    auto resources = runtime.snapshot();
    MeshRuntimeCache legacy(resources);

    std::cout
        << "token\tslug\tunits_per_meter\tyaw_degrees\tlegacy_xyz\t"
           "modern_xyz\tscale_xyz(modern/legacy)\tlegacy_center\tmodern_center\n";

    for (const auto& definition : modernTokenDefinitions())
    {
        const auto glb =
            modernRoot / std::filesystem::path(definition.relativeGlbPath)
                .lexically_relative("assets/modern");

        std::error_code filesystemError;
        if (!std::filesystem::is_regular_file(glb, filesystemError))
            continue;

        const auto legacyId =
            representativeLegacyMesh(definition.token);
        auto legacyAsset = legacy.resolve(legacyId);
        if (!legacyAsset)
        {
            std::cerr << definition.slug << ": legacy: "
                      << legacyAsset.error().detail << '\n';
            continue;
        }

        ModernGltfLoadOptions loadOptions;
        loadOptions.unitsPerMeter = definition.unitsPerMeter;
        loadOptions.yawDegrees = definition.yawDegrees;
        loadOptions.localOffset = definition.localOffset;
        loadOptions.groundToZero = true;
        auto modernAsset = loadModernGltfMesh(glb, loadOptions);
        if (!modernAsset)
        {
            std::cerr << definition.slug << ": modern: "
                      << modernAsset.error().detail << '\n';
            continue;
        }

        const auto oldDimensions =
            dimensions((*legacyAsset)->renderData->bounds);
        const auto newDimensions =
            dimensions((*modernAsset)->bounds);

        std::array<float, 3> ratio{};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            ratio[axis] = oldDimensions.size[axis] != 0.0F
                ? newDimensions.size[axis] / oldDimensions.size[axis]
                : 0.0F;
        }

        std::cout << static_cast<unsigned>(definition.token)
                  << '\t' << definition.slug
                  << '\t' << definition.unitsPerMeter
                  << '\t' << definition.yawDegrees << '\t';
        printVector(oldDimensions.size);
        std::cout << '\t';
        printVector(newDimensions.size);
        std::cout << '\t';
        printVector(ratio);
        std::cout << '\t';
        printVector(oldDimensions.center);
        std::cout << '\t';
        printVector(newDimensions.center);
        std::cout << '\n';
    }

    std::array<std::set<std::size_t>, ModernTokenCount> poseCounts;
    std::array<std::size_t, ModernTokenCount> hmdCounts{};
    std::array<std::size_t, ModernTokenCount> decodeFailures{};

    for (std::uint32_t rawTag = 0; rawTag <= 0x00E2U; ++rawTag)
    {
        const auto id = packDataId(
            LegacyGroupId::ThreeD,
            static_cast<DataTag>(rawTag));
        const auto token = tokenForLegacyMesh(id);
        if (!token)
            continue;

        ++hmdCounts[*token];
        auto asset = legacy.resolve(id);
        if (!asset || !(*asset)->mesh)
        {
            ++decodeFailures[*token];
            continue;
        }
        poseCounts[*token].insert((*asset)->mesh->poseCount());
    }

    std::cout << "\nretail MIMe pose inventory\n"
              << "token\tslug\thmd_count\tpose_counts\tdecode_failures\n";
    for (const auto& definition : modernTokenDefinitions())
    {
        const auto token = static_cast<std::size_t>(definition.token);
        std::cout << static_cast<unsigned>(definition.token)
                  << '\t' << definition.slug
                  << '\t' << hmdCounts[token] << '\t';
        bool first = true;
        for (const auto count : poseCounts[token])
        {
            if (!first) std::cout << ',';
            std::cout << count;
            first = false;
        }
        std::cout << '\t' << decodeFailures[token] << '\n';
    }

    constexpr DataTag CornerBase = 0x00F6;
    constexpr DataTag MoveBase = 0x010D;
    constexpr std::uint32_t AnimsPerToken = 0x0159 - 0x00F6;

    std::cout << "\nretail token sequence mesh inventory\n"
              << "token\tslug\tidle_sequence\tidle_hmds\t"
                 "movement_hmd_count\tmovement_hmds\n";

    for (const auto& definition : modernTokenDefinitions())
    {
        const auto token = static_cast<std::uint32_t>(definition.token);
        const auto idleTag = static_cast<DataTag>(
            static_cast<std::uint32_t>(MoveBase) +
            token * AnimsPerToken);
        const auto idleId = packDataId(LegacyGroupId::ThreeD, idleTag);

        std::set<DataTag> idleMeshes;
        std::set<DataTag> movementMeshes;

        const auto collectMeshes =
            [&](DataId sequenceId, std::set<DataTag>& output)
        {
            auto program = monopoly::sequence::SequenceProgram::load(
                resources, sequenceId);
            if (!program)
                return;
            for (const auto& description : (*program)->descriptions())
            {
                if (!description.contentsDataId)
                    continue;
                const auto mapped =
                    tokenForLegacyMesh(*description.contentsDataId);
                if (mapped && *mapped == definition.token)
                    output.insert(dataTag(*description.contentsDataId));
            }
        };

        collectMeshes(idleId, idleMeshes);

        // Movement code uses move base + 0..10 and corner base + 0..4.
        // Both bases are offset by the same 99-sequence token stride.
        for (std::uint32_t offset = 0; offset <= 10; ++offset)
        {
            collectMeshes(
                packDataId(
                    LegacyGroupId::ThreeD,
                    static_cast<DataTag>(
                        static_cast<std::uint32_t>(MoveBase) +
                        token * AnimsPerToken + offset)),
                movementMeshes);
        }
        for (std::uint32_t offset = 0; offset <= 4; ++offset)
        {
            collectMeshes(
                packDataId(
                    LegacyGroupId::ThreeD,
                    static_cast<DataTag>(
                        static_cast<std::uint32_t>(CornerBase) +
                        token * AnimsPerToken + offset)),
                movementMeshes);
        }

        const auto printTags = [](const std::set<DataTag>& tags)
        {
            bool first = true;
            for (const auto tag : tags)
            {
                if (!first) std::cout << ',';
                std::cout << "0x" << std::hex
                          << static_cast<unsigned>(tag)
                          << std::dec;
                first = false;
            }
        };

        std::cout << static_cast<unsigned>(definition.token)
                  << '\t' << definition.slug
                  << "\t0x" << std::hex
                  << static_cast<unsigned>(idleTag)
                  << std::dec << '\t';
        printTags(idleMeshes);
        std::cout << '\t' << movementMeshes.size() << '\t';
        printTags(movementMeshes);
        std::cout << '\n';
    }

    return 0;
}
