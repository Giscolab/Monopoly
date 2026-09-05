#pragma once

#include "ResourcePaths.hpp"

#include <filesystem>
#include <vector>

namespace monopoly::udutils
{
    bool generateINIFile();

    const std::vector<std::filesystem::path>& searchPaths();
    const data::ResourcePaths* resourcePaths() noexcept;
}
