#pragma once

#include "LayeredDataSource.hpp"

#include <expected>
#include <filesystem>
#include <vector>

namespace monopoly::data
{
    // Loads a tab-separated manifest of uncompressed logical DATA payloads.
    // Columns: group, tag, LegacyDataType name, relative payload path.
    // These files replace DAT container entries, not their decoded semantics.
    [[nodiscard]] std::expected<
        std::vector<DataSourceOverride>,
        DataError>
    loadLooseDataOverrides(const std::filesystem::path& manifestPath);
}
