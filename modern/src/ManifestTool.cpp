#include "LegacyManifest.hpp"

#include <filesystem>
#include <iostream>
#include <vector>

int main(int argumentCount, char** arguments)
{
    using namespace monopoly::data;

    if (argumentCount < 3)
    {
        std::cerr
            << "usage: MonopolyManifestTool <output.tsv> <DMake-header>...\n";
        return 2;
    }

    std::vector<LegacyBankManifest> manifests;
    manifests.reserve(static_cast<std::size_t>(argumentCount - 2));

    for (int index = 2; index < argumentCount; ++index)
    {
        auto manifest = readDmakeManifest(arguments[index]);

        if (!manifest)
        {
            std::cerr
                << manifestErrorCodeName(manifest.error().code)
                << ": " << manifest.error().path.string()
                << ':' << manifest.error().line
                << ": " << manifest.error().detail << '\n';
            return 1;
        }

        manifests.push_back(std::move(*manifest));
    }

    auto written = writeManifestTsv(arguments[1], manifests);

    if (!written)
    {
        std::cerr
            << manifestErrorCodeName(written.error().code)
            << ": " << written.error().path.string()
            << ": " << written.error().detail << '\n';
        return 1;
    }

    return 0;
}
