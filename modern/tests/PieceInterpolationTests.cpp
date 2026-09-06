#include "PieceInterpolation.hpp"

#include <cmath>
#include <iostream>

using namespace monopoly::pieces;

namespace
{
    int failures{};
    void expect(bool condition, const char* message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) ++failures;
    }
    bool near(float a, float b, float epsilon = 0.001F)
    { return std::fabs(a - b) <= epsilon; }

    void testSegmentBookkeeping()
    {
        PieceInterpolationPath path{};
        expect(addBezierSegment(path, 0, {0,0,0}, {2,0,0}, {8,0,0}, {10,0,0}),
            "first Bezier segment is accepted");
        expect(path.used == 1 && near(path.lengths[0], 10.0F),
            "segment count and three-chord length match UDUtils");
        expect(addBezierSegmentSmooth(path, 1, {18,0,0}, {20,0,0}),
            "smooth continuation accepts a non-first segment");
        expect(path.used == 2 && path.segments[1][0] == InterpolationVec3{10,0,0} &&
            path.segments[1][1] == InterpolationVec3{12,0,0},
            "smooth continuation reflects the previous C point around D");
        expect(!addBezierSegmentSmooth(path, 0, {}, {}),
            "smooth continuation rejects the first segment like the source");
    }

    void testSamplingContract()
    {
        PieceInterpolationPath path{};
        path.startTick = 100;
        path.endTick = 200;
        addBezierSegment(path, 0, {0,0,0}, {0,0,0}, {10,0,0}, {10,0,0});

        const auto start = samplePieceInterpolation(path, 100);
        const auto quarter = samplePieceInterpolation(path, 125);
        const auto end = samplePieceInterpolation(path, 200);
        expect(near(start.location.x, 0.0F) && near(end.location.x, 10.0F),
            "Bezier sampling clamps to exact endpoints");
        expect(near(quarter.ratio, 0.1464466F, 0.0002F),
            "accelerate-in/out bends a quarter tick ratio with the source cosine formula");
        expect(quarter.forward.x > 0.99F && near(quarter.up.y, 1.0F),
            "tangent direction produces source-style forward/up vectors");
    }

    void testLengthWeightedSegments()
    {
        PieceInterpolationPath path{};
        path.speed = InterpolationSpeed::Linear;
        path.startTick = 0;
        path.endTick = 40;
        addBezierSegment(path, 0, {0,0,0}, {3,0,0}, {7,0,0}, {10,0,0});
        addBezierSegment(path, 1, {10,0,0}, {20,0,0}, {30,0,0}, {40,0,0});
        const auto first = samplePieceInterpolation(path, 9);
        const auto second = samplePieceInterpolation(path, 11);
        expect(first.segment == 0 && second.segment == 1,
            "Bezier segment time is split by approximate chord length");
    }
}

int main()
{
    testSegmentBookkeeping();
    testSamplingContract();
    testLengthWeightedSegments();
    std::cout << (failures ? "Piece interpolation tests FAILED\n" :
        "Piece interpolation tests passed\n");
    return failures ? 1 : 0;
}
