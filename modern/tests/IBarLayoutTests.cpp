#include "IBarLayout.hpp"

#include <iostream>
#include <string_view>

namespace
{
    int failures = 0;


    void expect(
        bool condition,
        std::string_view description)
    {
        if (condition)
        {
            std::cout
                << "[PASS] "
                << description
                << '\n';

            return;
        }


        ++failures;

        std::cerr
            << "[FAIL] "
            << description
            << '\n';
    }


    void testConstants()
    {
        using namespace
            monopoly::ibar::layout;


        expect(
            ScoreY == 560,
            "DISPLAY_ScoreY == 560"
        );


        expect(
            ScoreBorder == 2,
            "DISPLAY_IBAR_ScoreBorder == 2"
        );


        expect(
            BankWidth == 45,
            "DISPLAY_IBAR_BankWidth == 45"
        );


        expect(
            ScoreBoxLargeWidth == 184,
            "large player box width == 184"
        );


        expect(
            ScoreBoxSmallWidth == 127,
            "small player box width == 127"
        );
    }


    void testWidths()
    {
        using namespace
            monopoly::ibar::layout;


        expect(
            scoreBoxWidth(1) == 184 &&
            scoreBoxWidth(2) == 184 &&
            scoreBoxWidth(3) == 184 &&
            scoreBoxWidth(4) == 184,
            "1..4 players use large score boxes"
        );


        expect(
            scoreBoxWidth(5) == 127 &&
            scoreBoxWidth(6) == 127,
            "5..6 players use small score boxes"
        );
    }


    void testExactPositions()
    {
        using namespace
            monopoly::ibar::layout;


        expect(
            scoreX(
                0,
                1,
                ScoreBoxLargeWidth
            ) == 285,
            "1-player X = 285"
        );


        expect(
            scoreX(0, 2, 184) == 129 &&
            scoreX(1, 2, 184) == 440,
            "2-player X positions"
        );


        expect(
            scoreX(0, 3, 184) == 51 &&
            scoreX(1, 3, 184) == 284 &&
            scoreX(2, 3, 184) == 517,
            "3-player X positions"
        );


        expect(
            scoreX(0, 4, 184) == 5 &&
            scoreX(1, 4, 184) == 192 &&
            scoreX(2, 4, 184) == 379 &&
            scoreX(3, 4, 184) == 566,
            "4-player X positions"
        );


        expect(
            scoreX(0, 5, 127) == 21 &&
            scoreX(1, 5, 127) == 167 &&
            scoreX(2, 5, 127) == 313 &&
            scoreX(3, 5, 127) == 459 &&
            scoreX(4, 5, 127) == 605,
            "5-player X positions"
        );


        // Le numérateur devient légèrement négatif à six
        // joueurs. C++ effectue une division entière vers zéro,
        // comme le Visual C++ du source original.
        expect(
            scoreX(0, 6, 127) == 1 &&
            scoreX(1, 6, 127) == 127 &&
            scoreX(2, 6, 127) == 253 &&
            scoreX(3, 6, 127) == 379 &&
            scoreX(4, 6, 127) == 505 &&
            scoreX(5, 6, 127) == 631,
            "6-player X positions"
        );
    }


    void testHitRects()
    {
        using namespace
            monopoly::ibar::layout;


        const Rect first =
            playerSetupHitRect(
                0,
                4
            );


        expect(
            first.left == 5 &&
            first.right == 189 &&
            first.top == 560 &&
            first.bottom == 600,
            "player setup hit rect geometry"
        );


        expect(
            first.contains(
                5,
                560
            ),
            "hit rect includes left/top"
        );


        expect(
            !first.contains(
                189,
                560
            ) &&
            !first.contains(
                5,
                600
            ),
            "hit rect excludes right/bottom"
        );
    }
}


int main()
{
    std::cout
        << "Monopoly UDIBar layout tests\n"
        << "============================\n";


    testConstants();

    testWidths();

    testExactPositions();

    testHitRects();


    std::cout << '\n';


    if (failures != 0)
    {
        std::cerr
            << failures
            << " UDIBar test(s) failed.\n";

        return 1;
    }


    std::cout
        << "All UDIBar tests passed.\n";


    return 0;
}
