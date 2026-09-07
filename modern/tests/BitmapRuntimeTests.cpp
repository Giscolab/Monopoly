#include "BitmapRuntime.hpp"
#include "SequenceWorld2DSlot.hpp"
#include "SyntheticSequenceResources.hpp"
#include <iostream>
#include <array>

int main()
{
    using namespace monopoly;
    int failures = 0;
    const auto check = [&](bool ok, const char* text) {
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << text << '\n';
        if (!ok) ++failures;
    };
    auto bytes = SyntheticSequenceResources::bitmap24();
    auto image = data::decodeLegacyBitmapRGBA8(bytes);
    const std::vector<std::uint8_t> expected{
        0,0,255,255, 255,255,255,255, 255,0,0,255, 0,255,0,255};
    check(image && image->width == 2 && image->height == 2 && image->pixels == expected,
        "bottom-up padded BMP24 decodes exact top-down RGBA, green stays opaque");
    check(!data::decodeLegacyBitmapRGBA8(bytes, 3), "pixel budget rejects before allocation");
    auto topDown = bytes;
    topDown[22] = std::byte{254};
    topDown[23] = topDown[24] = topDown[25] = std::byte{255};
    std::swap_ranges(topDown.begin()+54, topDown.begin()+62, topDown.begin()+62);
    auto topImage = data::decodeLegacyBitmapRGBA8(topDown);
    check(topImage && topImage->pixels == expected, "negative height uses top-down row order");
    bytes.pop_back();
    check(!data::decodeLegacyBitmapRGBA8(bytes), "truncated final padded row rejected");
    auto palette = SyntheticSequenceResources::bitmap24();
    palette.resize(70);
    palette[28] = std::byte{8}; palette[10] = std::byte{62};
    palette[34] = std::byte{8}; palette[46] = std::byte{2};
    const std::array<unsigned,16> pal{0,0,255,0, 0,255,0,255, 0,1,0,0, 1,0,0,0};
    for (std::size_t i=0;i<pal.size();++i) palette[54+i]=static_cast<std::byte>(pal[i]);
    auto indexed = data::decodeLegacyBitmapRGBA8(palette);
    check(indexed && indexed->pixels == std::vector<std::uint8_t>{
        0,255,0,255,255,0,0,255,255,0,0,255,0,255,0,255},
        "indexed BMP ignores palette reserved byte and retains opaque index zero");
    palette[62]=std::byte{2};
    check(!data::decodeLegacyBitmapRGBA8(palette), "invalid palette index rejected");
    palette[46]=std::byte{3};
    check(!data::decodeLegacyBitmapRGBA8(palette), "palette overlap rejected");
    const auto uapBytes = SyntheticSequenceResources::uap8();
    const auto uapMeta = data::inspectLegacyUap(uapBytes);
    check(uapMeta && uapMeta->width == 3 && uapMeta->height == 2 &&
            uapMeta->originX == -7 && uapMeta->originY == 13 &&
            uapMeta->rowStride == 4,
        "UAP NEWBITMAPHEADER preserves dimensions, signed origin and DWORD stride");
    const auto uapImage = data::decodeLegacyUapRGBA8(uapBytes);
    check(uapImage && uapImage->pixels == std::vector<std::uint8_t>{
        0,0,0,0, 255,0,0,128, 0,255,0,255,
        0,0,255,255, 0,255,0,255, 255,0,0,128},
        "UAP decodes top-down palette, colour key and premultiplied alpha to straight RGBA8");
    check(!data::decodeLegacyUapRGBA8(uapBytes, 5),
        "UAP pixel budget rejects before allocation");
    auto badUap = uapBytes; badUap.pop_back();
    check(!data::inspectLegacyUap(badUap),
        "truncated UAP padded raster is rejected");

    data::BitmapRuntimeCache cache;
    auto source = std::make_shared<const data::DataBytes>(SyntheticSequenceResources::bitmap24());
    auto first = cache.resolve(7, data::LegacyDataType::Bitmap, source);
    auto same = cache.resolve(7, data::LegacyDataType::Bitmap, source);
    check(first && same && *first == *same && cache.size()==1, "immutable bitmap cache reuse");
    auto invalid = cache.resolve(7, data::LegacyDataType::Bitmap, std::make_shared<const data::DataBytes>());
    check(!invalid && cache.resolve(7, data::LegacyDataType::Bitmap, source).value()==first.value(), "failed replacement preserves old cache asset");
    auto replacement = cache.resolve(7, data::LegacyDataType::Bitmap, std::make_shared<const data::DataBytes>(*source));
    check(replacement && *replacement != *first && (*first)->image.pixels==expected,
        "same DataId new snapshot payload replaces cache while old lease survives");
    auto uapSource = std::make_shared<const data::DataBytes>(uapBytes);
    auto uapAsset = cache.resolve(8, data::LegacyDataType::Uap, uapSource);
    check(uapAsset && (*uapAsset)->sourceType == data::LegacyDataType::Uap &&
            (*uapAsset)->image.width == 3 && (*uapAsset)->image.height == 2,
        "bitmap runtime cache decodes and identifies immutable DataUAP assets");
    engine::SequenceWorld2DSlot slot;
    sequence::SequenceBitmapRenderItem item{1,7,257,0,sequence::translate2D(-11,0),
        {data::LegacyDataType::Bitmap,2,2,0,0,24},source};
    auto low=item; low.node=2; low.priority=256; low.worldTransform=sequence::translate2D(-35,0);
    auto stats=slot.sync({item,low},cache);
    check(stats && stats->started==2 && slot.order()==std::vector<sequence::SequenceNodeId>{1,2},
        "slot preserves sequencer traversal and parent priority grouping");
    stats=slot.sync({item,low},cache);
    check(stats && stats->unchanged==2, "unchanged slot preserves cached assets");
    item.worldTransform=sequence::translate2D(4,5);
    stats=slot.sync({item},cache);
    check(stats && stats->moved==1 && stats->stopped==1, "move and shutdown lifecycle");
    check(!slot.sync({item,item},cache) && slot.size()==1,
        "duplicate node rejects transaction without replacing old slot");
    auto bad=low; bad.node=3; bad.bytes=std::make_shared<const data::DataBytes>();
    check(!slot.sync({low,bad},cache) && slot.size()==1, "invalid publication preserves previous slot");
    cache.clear();
    check(slot.find(1) && slot.find(1)->asset->image.pixels==expected,
        "slot retains immutable pixels after cache clear");
    return failures ? 1 : 0;
}
