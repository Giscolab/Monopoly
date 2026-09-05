#pragma once

#include "LegacyDataArchiveBuilder.hpp"
#include "ResourceRuntime.hpp"
#include <chrono>
#include <stdexcept>

// Test-only DAT/HMD data. Never installed or used by MonopolyModern.
struct SyntheticSequenceResources
{
    std::filesystem::path directory = std::filesystem::current_path() /
        ("SequenceGPU-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    monopoly::data::ResourceRuntime service;
    static monopoly::data::DataBytes words(std::initializer_list<std::uint32_t> values)
    {
        monopoly::data::DataBytes bytes;
        for (auto value : values)
            for (unsigned shift = 0; shift < 32; shift += 8)
                bytes.push_back(static_cast<std::byte>((value >> shift) & 255U));
        return bytes;
    }
    SyntheticSequenceResources()
    {
        using namespace monopoly::data;
        std::filesystem::create_directories(directory / "Dat_Mon");
        const std::array names{"dat_main.dat", "dat_pat.dat", "dat_bord.dat", "dat_brd2.dat",
            "dat_3d.dat", "dat_ln01.dat", "dat_lm01.dat", "dat_lk01.dat"};
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            std::vector<ArchiveBuildItem> items;
            if (i == 0)
                items.push_back({LegacyDataType::Chunky,
                    words({0x09000014, 0, 0x44000000, 18,
                        packDataId(LegacyGroupId::ThreeD, 0)})});
            else if (i == 4)
            {
                const auto mesh = words({0x01020304,0,6,2,11,0,1,3,0x80000011,0x80000014,0x8000001A,
                    0xFFFFFFFF,7,0x80000001,8,0x80010002,0,0x000000FF,0,0x00020001,
                    0x0002FFFE,10,0x00020002,10,0xFFFE0000,10,0,4096});
                // Tags 0..3 include the exact startup board tag HMD_boardmed=3.
                // Reusing synthetic geometry keeps the test about routing, not retail assets.
                for (int tag = 0; tag <= 3; ++tag)
                    items.push_back({LegacyDataType::Hmd, mesh});
            }
            else if (i == 5)
            {
                items.push_back({LegacyDataType::IndexTable,
                    {std::byte{42},std::byte{0},std::byte{0},std::byte{0},std::byte{1},std::byte{0}}});
                items.push_back({LegacyDataType::String,
                    {std::byte{'A'},std::byte{0},std::byte{0},std::byte{0}}});
            }
            else items.push_back({LegacyDataType::Native, {std::byte{1}}});
            if (!writeLegacyDataArchive(directory / "Dat_Mon" / names[i], items))
                throw std::runtime_error("synthetic DAT write failed");
        }
        auto paths = ResourcePaths::create(std::array{directory});
        if (!paths || !service.initialize(*paths))
            throw std::runtime_error("synthetic resource snapshot failed");
    }
    ~SyntheticSequenceResources()
    {
        service.shutdown();
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};
