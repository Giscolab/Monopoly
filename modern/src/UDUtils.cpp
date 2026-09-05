#include "UDUtils.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <optional>

namespace monopoly::udutils
{
    namespace
    {
        std::vector<std::filesystem::path> globalSearchPaths;
        std::optional<data::ResourcePaths> globalResourcePaths;

        void addPath(const std::filesystem::path& path)
        {
            if (std::find(
                    globalSearchPaths.begin(),
                    globalSearchPaths.end(),
                    path) == globalSearchPaths.end())
            {
                globalSearchPaths.push_back(path);
            }
        }
    }

    bool generateINIFile()
    {
        globalSearchPaths.clear();
        globalResourcePaths.reset();

        const char* basePath = SDL_GetBasePath();

        if (basePath == nullptr)
        {
            return false;
        }

        // Une installation de donnees explicite remplace la racine executable.
        // Aucun cwd ou chemin CD/machine implicite ne fournit des banques.
        const char* overrideRoot = SDL_getenv("MONOPOLY_DATA_ROOT");
        const std::string_view rootText{
            overrideRoot != nullptr ? overrideRoot : basePath };
        std::u8string utf8Root;
        utf8Root.reserve(rootText.size());
        for (const unsigned char byte : rootText)
        {
            utf8Root.push_back(static_cast<char8_t>(byte));
        }
        const std::filesystem::path modulePath{ utf8Root };
        const std::array roots{ modulePath };
        auto resolved = data::ResourcePaths::create(roots);
        if (!resolved)
        {
            std::cerr << "Resource root configuration failed: "
                << resolved.error().detail << '\n';
            return false;
        }
        globalResourcePaths = std::move(*resolved);

        // Correspondance avec UDUTILS_GenerateINIFile() original.
        addPath(modulePath);
        addPath(modulePath / "Dat_Mon");
        addPath(modulePath / "AVI");

        return true;
    }

    const std::vector<std::filesystem::path>& searchPaths()
    {
        return globalSearchPaths;
    }


    const data::ResourcePaths* resourcePaths() noexcept
    {
        return globalResourcePaths ? &*globalResourcePaths : nullptr;
    }
}
