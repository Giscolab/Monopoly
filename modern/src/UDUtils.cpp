#include "UDUtils.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace monopoly::udutils
{
    namespace
    {
        std::vector<std::filesystem::path> globalSearchPaths;

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

        const char* basePath = SDL_GetBasePath();

        if (basePath == nullptr)
        {
            return false;
        }

        const std::filesystem::path modulePath{ basePath };

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
}
