#include "MeshRuntime.hpp"
#include "ModernGltfMesh.hpp"
#include "ModernTokenCatalog.hpp"
#include "ResourcePaths.hpp"
#include "ResourceRuntime.hpp"

#include <array>
#include <filesystem>
#include <iomanip>
#include <iostream>
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

    return 0;
}
