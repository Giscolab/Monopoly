#pragma once
#include "LegacyBitmap.hpp"
#include <algorithm>

namespace monopoly::menu
{
    // For flat 3x shells only: horizontal predicates and source alpha must be
    // constant over each aligned triplet. Evaluate every output row separately;
    // the integer vertical gradient can change inside a three-row block.
    // Photo sampling and token compositing must use the full-resolution path.
    template<class PaintPixel>
    void paintMenuRaster(data::LegacyBitmapRGBA8& image, bool horizontalTriplets,
        PaintPixel&& paintPixel)
    {
        const unsigned step = horizontalTriplets && image.width % 3 == 0 ? 3 : 1;
        for (unsigned y = 0; y < image.height; ++y)
            for (unsigned x = 0; x < image.width; x += step)
            {
                const auto offset = (std::size_t(y) * image.width + x) * 4;
                paintPixel(x, y, offset);
                if (step == 3)
                {
                    std::copy_n(image.pixels.data() + offset, 4, image.pixels.data() + offset + 4);
                    std::copy_n(image.pixels.data() + offset, 4, image.pixels.data() + offset + 8);
                }
            }
    }
}
