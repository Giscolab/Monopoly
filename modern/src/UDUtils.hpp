#pragma once

#include <filesystem>
#include <vector>

namespace monopoly::udutils
{
    bool generateINIFile();

    const std::vector<std::filesystem::path>& searchPaths();
}
