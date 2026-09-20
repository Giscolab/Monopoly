#pragma once

#include "LegacyMeshData.hpp"
#include "ResourcePaths.hpp"
#include "TextureCatalog.hpp"

#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace monopoly::data
{
    // Loads a complete immutable substitution batch. Any missing or invalid
    // base/overlay fails the batch so callers can retain the previous mesh.
    // Embedded recipes need no external resources and return an empty batch.
    [[nodiscard]] std::expected<
        std::vector<std::shared_ptr<const HmdTextureImage>>, std::string>
    loadBoardTextureImages(
        const ResourcePaths& paths,
        const BoardTextureRecipe& recipe,
        const BoardTextureContext& context);
}
