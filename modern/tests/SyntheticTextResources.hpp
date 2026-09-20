#pragma once

#include "FontRuntime.hpp"
#include "SyntheticSequenceResources.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <map>
#include <string_view>

// Test-only container payloads with explicit sentinel text. They prove numeric
// LANG lookup and formatting through the real archive/catalog; they never claim
// to reconstruct retail translations, artwork or installed game assets.
struct SyntheticTextResources : SyntheticSequenceResources
{
    using Texts = std::map<std::uint32_t, std::u16string>;
    using Replacements = std::map<monopoly::data::DataTag, monopoly::data::ArchiveBuildItem>;

    explicit SyntheticTextResources(const Texts& texts = {}, const Replacements& main = {})
    {
        using namespace monopoly::data;
        service.shutdown();
        const auto append = [](DataBytes& bytes, std::uint32_t value, unsigned count)
        {
            for (unsigned i = 0; i < count; ++i)
                bytes.push_back(static_cast<std::byte>((value >> (i * 8)) & 255u));
        };
        if (!texts.empty())
        {
            std::vector<ArchiveBuildItem> items{{LegacyDataType::IndexTable, {}}};
            for (const auto& [id, text] : texts)
            {
                append(items[0].payload, id, 4);
                append(items[0].payload, static_cast<std::uint32_t>(items.size()), 2);
                DataBytes bytes;
                for (const auto value : text) append(bytes, value, 2);
                append(bytes, 0, 2);
                items.push_back({LegacyDataType::String, std::move(bytes)});
            }
            if (!writeLegacyDataArchive(directory / "Dat_Mon/dat_ln01.dat", items))
                throw std::runtime_error("synthetic explicit LANG archive write failed");
        }
        if (!main.empty())
        {
            const auto path = directory / "Dat_Mon/dat_main.dat";
            const auto opened = LegacyDataArchive::open(path, static_cast<std::uint16_t>(LegacyGroupId::Main));
            if (!opened) throw std::runtime_error(opened.error().detail);
            auto archive = *opened;
            std::vector<ArchiveBuildItem> items(archive->itemCount());
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                const auto meta = archive->metadata(static_cast<DataTag>(i));
                if (!meta) throw std::runtime_error(meta.error().detail);
                if (!meta->present()) continue;
                const auto bytes = archive->load(static_cast<DataTag>(i));
                if (!bytes) throw std::runtime_error(bytes.error().detail);
                items[i] = {meta->type, **bytes};
            }
            archive->close();
            for (const auto& [tag, item] : main)
            {
                if (items.size() <= tag) items.resize(static_cast<std::size_t>(tag) + 1);
                items[tag] = item;
            }
            if (!writeLegacyDataArchive(path, items))
                throw std::runtime_error("synthetic stock surface archive write failed");
        }
        const auto paths = ResourcePaths::create(std::array{directory});
        if (!paths || !service.initialize(*paths))
            throw std::runtime_error("synthetic text resource snapshot failed");
    }

    static monopoly::data::DataBytes stockUap(std::uint16_t width, std::uint16_t height)
    {
        monopoly::data::DataBytes bytes;
        const auto append = [&](std::uint32_t value, unsigned count)
        {
            for (unsigned i = 0; i < count; ++i)
                bytes.push_back(static_cast<std::byte>((value >> (i * 8)) & 255u));
        };
        append(width, 2); append(height, 2); append(0, 2); append(0, 2);
        append(2, 4); append(2, 2); append(2, 2); // alpha palette, two entries.
        append(0, 4); append(0, 4); // transparent index zero.
        append(0x00141E28, 4); append(255, 4); // visible BGR stock sentinel.
        const std::size_t pitch = (static_cast<std::size_t>(width) + 3u) & ~std::size_t{3};
        bytes.insert(bytes.end(), pitch * height, std::byte{1});
        return bytes;
    }
};

inline void loadRealTestArial(monopoly::fonts::Runtime& font)
{
    std::vector<std::filesystem::path> roots;
    if (const char* base = SDL_GetBasePath(); base && *base) roots.emplace_back(base);
#ifdef _WIN32
    if (const char* windows = std::getenv("WINDIR"); windows && *windows)
        roots.emplace_back(std::filesystem::path(windows) / "Fonts");
#endif
    const auto path = monopoly::fonts::resolveRetailArial(roots);
    if (!path) throw std::runtime_error("real Arial required; no font skip or substitute: " + path.error().detail);
    const auto selected = font.setFont(*path, "Arial");
    if (!selected) throw std::runtime_error(selected.error().detail);
    const auto sized = font.setSize(12);
    if (!sized) throw std::runtime_error(sized.error().detail);
    font.setWeight(700);
}
