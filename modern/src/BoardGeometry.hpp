#pragma once

#include <array>

namespace monopoly::boardgeometry
{
    struct Point2D
    {
        int x;
        int y;
    };

    struct Point3D
    {
        float x;
        float y;
        float z;
    };

    const std::array<Point2D, 42>&
        locations2D();

    const std::array<Point3D, 42>&
        locations3D();
}
