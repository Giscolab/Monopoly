#include "ArchiveAssembly.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    void usage()
    {
        std::cout << "MonopolyArchiveTool --source <Source/monopoly> --output <workspace>\n"
            "    [--extra-root <directory>]... [--map <mapping.tsv>] [--strict]\n\n"
            "TSV columns: bank<TAB>symbol<TAB>root<TAB>path\n"
            "Root 0 is --source, other roots follow --extra-root order.\n"
            "Reuses supplied disk payloads and converts compatible English.atr text.\n"
            "No missing assets are fabricated. Partial banks remain isolated.\n"
            "Exit 0: assembly/report produced (may be partial); 1: input/I/O error;\n"
            "     2: CLI error; 3: --strict requested but a bank is incomplete.\n";
    }
}

int main(int argc, char** argv)
{
    using namespace monopoly::data::assembly;
    Request request;
    for (int n=1;n<argc;++n)
    {
        const std::string_view key=argv[n];
        if (key=="--help") { usage(); return 0; }
        if (key=="--strict") { request.requireComplete=true; continue; }
        if (key!="--source" && key!="--output" && key!="--extra-root" && key!="--map")
        { std::cerr << "Unknown option: " << key << '\n'; usage(); return 2; }
        if (++n==argc) { std::cerr << "Missing value for " << key << '\n'; return 2; }
        const std::string_view value=argv[n];
        const auto path=std::filesystem::path(std::u8string(value.begin(),value.end()));
        if (key=="--source") request.sourceRoot=path;
        else if (key=="--output") request.outputRoot=path;
        else if (key=="--map") request.mappings=path;
        else request.extraRoots.push_back(path);
    }
    if (request.sourceRoot.empty() || request.outputRoot.empty()) { usage(); return 2; }
    try
    {
        const auto result=run(request);
        return request.requireComplete && !result.complete() ? 3 : 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Archive assembly failed: " << error.what() << '\n';
        return 1;
    }
}
