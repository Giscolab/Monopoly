#pragma once

#include "LegacyDataArchiveBuilder.hpp"
#include "ResourceRuntime.hpp"
#include "RuleTypes.hpp"
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
    static monopoly::data::DataBytes bitmap24()
    {
        monopoly::data::DataBytes bytes;
        const auto u16 = [&](std::uint16_t value) {
            bytes.push_back(static_cast<std::byte>(value & 255U));
            bytes.push_back(static_cast<std::byte>((value >> 8U) & 255U));
        };
        const auto u32 = [&](std::uint32_t value) {
            for (unsigned shift = 0; shift < 32; shift += 8)
                bytes.push_back(static_cast<std::byte>((value >> shift) & 255U));
        };
        bytes.push_back(std::byte{'B'}); bytes.push_back(std::byte{'M'});
        u32(70); u16(0); u16(0); u32(54); u32(40);
        u32(2); u32(2); u16(1); u16(24); u32(0); u32(16);
        u32(0); u32(0); u32(0); u32(0);
        // Bottom row: red, green; top row: blue, white. BGR24 + 2-byte row pad.
        for (const auto value : {0U,0U,255U, 0U,255U,0U, 0U,0U,
                                 255U,0U,0U, 255U,255U,255U, 0U,0U})
            bytes.push_back(static_cast<std::byte>(value));
        return bytes;
    }
    explicit SyntheticSequenceResources(bool anchoredBitmaps = false)
    {
        using namespace monopoly::data;
        std::filesystem::create_directories(directory / "Dat_Mon");
        const std::array names{"dat_main.dat", "dat_pat.dat", "dat_bord.dat", "dat_brd2.dat",
            "dat_3d.dat", "dat_ln01.dat", "dat_lm01.dat", "dat_lk01.dat"};
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            std::vector<ArchiveBuildItem> items;
            if (i == 0)
            {
                items.push_back({LegacyDataType::Chunky,
                    words({0x09000014, 0, 0x44000000, 18,
                        packDataId(LegacyGroupId::ThreeD, 0)})});
                // Finite sequence: end=100, cadence=4, keep-frames on disk,
                // ending action Stop. Piece playback must override both.
                items.push_back({LegacyDataType::Chunky,
                    words({0x09000014, 0, 0x04000064, 17,
                        packDataId(LegacyGroupId::ThreeD, 0)})});
                // Synthetic DAT_MAIN dice 2D sequences. Chunk 3 is a bitmap leaf;
                // tag 0x00A0 is test-only bitmap payload, never a retail substitute.
                const auto bitmapSequence = words({0x03000014, 0, 0x04000000, 2, 0x000000A0});
                const auto finiteCardSequence = words({0x03000014, 0, 0x04000004, 2, 0x000000A0});
                items.resize(0x00A1);
                // USA Chance / Community deck fly-off sequences: 39 camera views each.
                for (std::uint32_t tag = 0x000FU; tag <= 0x005CU; ++tag)
                    items[tag] = {LegacyDataType::Chunky, finiteCardSequence};
                for (std::uint32_t tag = 0x0096U; tag <= 0x009CU; ++tag)
                    items[tag] = {LegacyDataType::Chunky, bitmapSequence};
                items[0x009D] = {LegacyDataType::Chunky,
                    words({0x03000014, 0, 0x04000000, 2, 0x00000001})};
                items[0x00A0] = {LegacyDataType::Bitmap, bitmap24()};
                if (anchoredBitmaps)
                {
                    // Synthetic child offset (400,300) makes negative root
                    // offsets visible; no anchor is inserted by the renderer.
                    auto anchored = words({0x01000035,0,0x04000064,2,0x81000005});
                    anchored.push_back(std::byte{2});
                    const auto child = words({0x03000020,0,0x04000064,2,0xA0,
                        0x8200000C,400,300});
                    anchored.insert(anchored.end(),child.begin(),child.end());
                    for (std::uint32_t tag=0x96;tag<=0x9C;++tag)
                        items[tag] = {LegacyDataType::Chunky, anchored};
                    items.resize(0xA2);
                    auto yellow = bitmap24();
                    for (const auto offset : {54,57,62,65})
                    { yellow[offset]=std::byte{0}; yellow[offset+1]=yellow[offset+2]=std::byte{255}; }
                    items[0xA1]={LegacyDataType::Bitmap,yellow};
                    auto second=anchored;
                    second[37]=std::byte{0xA1}; // child content DataId, relative DAT_MAIN
                    items[0x97]={LegacyDataType::Chunky,second};
                }
                // Active UDIBar backdrop sequences: TAB_indsbg0..TAB_indsbg7.
                // They deliberately reuse the synthetic bitmap payload above.
                items.resize(0x01D7);
                // UDIBar current-player token sequences: CNK_indstra + token.
                for (std::uint32_t tag = 0x005FU; tag < 0x005FU + monopoly::rules::MaxTokens; ++tag)
                    items[tag] = {LegacyDataType::Chunky, bitmapSequence};
                for (std::uint32_t tag = 0x015BU; tag <= 0x0162U; ++tag)
                    items[tag] = {LegacyDataType::Chunky, bitmapSequence};
                // UDIBar property title tabs: full colour, low colour, mortgaged.
                for (std::uint32_t tag = 0x0163U; tag <= 0x01B6U; ++tag)
                    items[tag] = {LegacyDataType::Chunky, bitmapSequence};
                // UDIBar score strip: jail bars, token atlas, large/small colour bars.
                for (std::uint32_t tag = 0x01BFU; tag <= 0x01D6U; ++tag)
                    items[tag] = {LegacyDataType::Chunky, bitmapSequence};
            }
            else if (i == 4)
            {
                const auto mesh = words({0x01020304,0,6,2,11,0,1,3,0x80000011,0x80000014,0x8000001A,
                    0xFFFFFFFF,7,0x80000001,8,0x80010002,0,0x000000FF,0,0x00020001,
                    0x0002FFFE,10,0x00020002,10,0xFFFE0000,10,0,4096});
                const auto finite = words({0x09000014, 0, 0x04000064, 17,
                    packDataId(LegacyGroupId::ThreeD, 0)});
                // Sparse synthetic DAT_3D: tags 0..3 remain HMD fixtures.
                // GoToJail plus center/resting transition tags are finite CNKs.
                items.resize(0x05CD);
                for (int tag = 0; tag <= 5; ++tag)
                    items[tag] = {LegacyDataType::Hmd, mesh};
                for (const auto tag : {0x0157, 0x0158, 0x05C7, 0x05C8,
                         0x05C9, 0x05CA, 0x05CB, 0x05CC})
                    items[tag] = {LegacyDataType::Chunky, finite};
                for (std::uint32_t tag = 0x0537U; tag <= 0x05C6U; ++tag)
                    items[tag] = {LegacyDataType::Chunky, finite};
                for (std::uint32_t token = 0; token < monopoly::rules::MaxTokens; ++token)
                    items[0x010DU + 0x63U * token] =
                        {LegacyDataType::Chunky, finite};
                for (std::uint32_t token = 0; token < monopoly::rules::MaxTokens; ++token)
                    for (std::uint32_t category = 0; category < 5; ++category)
                        for (std::uint32_t slot = 0; slot < 6; ++slot)
                        {
                            const auto outTag = 0x011BU + 0x63U * token +
                                2U * slot + 12U * category;
                            items[outTag] = {LegacyDataType::Chunky, finite};
                            items[outTag + 1U] = {LegacyDataType::Chunky, finite};
                        }
            }
            else if (i == 5)
            {
                items.push_back({LegacyDataType::IndexTable,
                    {std::byte{42},std::byte{0},std::byte{0},std::byte{0},std::byte{1},std::byte{0}}});
                items.push_back({LegacyDataType::String,
                    {std::byte{'A'},std::byte{0},std::byte{0},std::byte{0}}});
            }
            else if (i == 6)
            {
                items.resize(0x02F7);
                const auto finiteButton = words({
                    0x03000014, 0, 0x04000004, 2, 0x000002F6});
                // USA Chance / Community card in/face/idle/out animations.
                for (std::uint32_t tag = 0x0008U; tag <= 0x0057U; ++tag)
                    items[tag] = {LegacyDataType::Chunky, finiteButton};
                for (std::uint32_t tag = 0x0059U; tag <= 0x0088U; ++tag)
                    items[tag] = {LegacyDataType::Chunky, finiteButton};
                // Complete contiguous full-colour and AI/remote action-button
                // atlases used by UDIBar.cpp (28 buttons x 4 modes).
                for (std::uint32_t tag = 0x008AU; tag <= 0x00F9U; ++tag)
                    items[tag] = {LegacyDataType::Chunky, finiteButton};
                for (std::uint32_t tag = 0x0106U; tag <= 0x0175U; ++tag)
                    items[tag] = {LegacyDataType::Chunky, finiteButton};
                items[0x02F5] = {LegacyDataType::Chunky,
                    words({0x03000014, 0, 0x04000000, 2, 0x000002F6})};
                items[0x02F6] = {LegacyDataType::Bitmap, bitmap24()};
                // USA deed pop-ups used by UDIBar property mouseover. City 0 only:
                // TAB_iyb00x00 (mortgaged) and TAB_iyf00x00 (normal), 28 deeds each.
                items.resize(0x0CEC);
                // Player property-bar Get Out of Jail cards: Chance / Community.
                for (std::uint32_t tag = 0x0992U; tag <= 0x0993U; ++tag)
                    items[tag] = {LegacyDataType::Chunky, finiteButton};
                for (std::uint32_t tag = 0x0B53U; tag <= 0x0B6EU; ++tag)
                    items[tag] = {LegacyDataType::Chunky, finiteButton};
                for (std::uint32_t tag = 0x0CD0U; tag <= 0x0CEBU; ++tag)
                    items[tag] = {LegacyDataType::Chunky, finiteButton};
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
