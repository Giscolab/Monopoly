#pragma once
#include "LegacyBitmap.hpp"

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
                    auto* pixel = image.pixels.data() + offset;
                    pixel[4] = pixel[8] = pixel[0];
                    pixel[5] = pixel[9] = pixel[1];
                    pixel[6] = pixel[10] = pixel[2];
                    pixel[7] = pixel[11] = pixel[3];
                }
            }
    }
}
