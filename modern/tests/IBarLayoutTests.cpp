#include "IBarLayout.hpp"

#include <array>
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

        const auto bank = bankHitRect();
        expect(bank.left == 755 && bank.top == 560 &&
                bank.right == 800 && bank.bottom == 592 &&
                bank.contains(755, 560) && bank.contains(799, 591) &&
                !bank.contains(800, 591) && !bank.contains(799, 592),
            "bank hit rectangle preserves [755,800)x[560,592) Win32 bounds");


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


    void testActionButtonRects()
    {
        using namespace monopoly::ibar::layout;

        constexpr std::array expected{
            Rect{ 10, 455,  69, 483},
            Rect{ 70, 455, 132, 483},
            Rect{148, 455, 252, 483},
            Rect{569, 455, 672, 483},
            Rect{256, 455, 357, 483},
            Rect{464, 455, 567, 483},
            Rect{361, 455, 463, 483},
            Rect{690, 456, 727, 483},
            Rect{732, 456, 789, 483}
        };

        bool generalExact = true;
        for (int index = 0; index < static_cast<int>(expected.size()); ++index)
        {
            const Rect actual = actionButtonRect(
                static_cast<ActionButtonSlot>(index));
            const Rect wanted = expected[static_cast<std::size_t>(index)];
            generalExact = generalExact &&
                actual.left == wanted.left && actual.top == wanted.top &&
                actual.right == wanted.right && actual.bottom == wanted.bottom;
        }
        expect(generalExact,
            "nine general action-button rectangles match UDIBar.cpp exactly");

        const Rect buyMain = actionButtonRect(
            ActionButtonSlot::Main, ActionButtonLayout::BuyAuction);
        const Rect buyGeneral3 = actionButtonRect(
            ActionButtonSlot::General3, ActionButtonLayout::BuyAuction);
        expect(buyMain.left == 251 && buyMain.right == 354 &&
                buyMain.top == 455 && buyMain.bottom == 483 &&
                buyGeneral3.left == 467 && buyGeneral3.right == 569,
            "BuyAuction overrides Main and General3 source rectangles");

        const Rect taxMain = actionButtonRect(
            ActionButtonSlot::Main, ActionButtonLayout::TaxDecision);
        const Rect taxGeneral3 = actionButtonRect(
            ActionButtonSlot::General3, ActionButtonLayout::TaxDecision);
        expect(taxMain.left == 254 && taxMain.right == 355 &&
                taxGeneral3.left == 464 && taxGeneral3.right == 567,
            "TaxDecision overrides Main and General3 source rectangles");

        const Rect tradeMain = actionButtonRect(
            ActionButtonSlot::Main, ActionButtonLayout::Trading);
        const Rect tradeGeneral2 = actionButtonRect(
            ActionButtonSlot::General2, ActionButtonLayout::Trading);
        const Rect tradeGeneral3 = actionButtonRect(
            ActionButtonSlot::General3, ActionButtonLayout::Trading);
        expect(tradeMain.left == 349 && tradeMain.top == 420 &&
                tradeMain.right == 449 && tradeMain.bottom == 448 &&
                tradeGeneral2.left == 224 && tradeGeneral2.right == 324 &&
                tradeGeneral3.left == 473 && tradeGeneral3.right == 573,
            "Trading overrides Main/General2/General3 at y=420..448");

        const Rect unaffected = actionButtonRect(
            ActionButtonSlot::Options, ActionButtonLayout::Trading);
        expect(unaffected.left == 10 && unaffected.top == 455 &&
                unaffected.right == 69 && unaffected.bottom == 483,
            "custom layouts preserve non-overridden general slots");

        expect(tradeMain.contains(349, 420) &&
                !tradeMain.contains(449, 420) &&
                !tradeMain.contains(349, 448),
            "action rectangles preserve Win32 right/bottom-exclusive hit semantics");


        const auto generalHit = actionButtonHit(361, 455);
        const auto buyHit = actionButtonHit(251, 455,
            ActionButtonLayout::BuyAuction,
            actionButtonBit(ActionButtonSlot::Main) |
                actionButtonBit(ActionButtonSlot::General3));
        const auto taxHit = actionButtonHit(464, 455,
            ActionButtonLayout::TaxDecision,
            actionButtonBit(ActionButtonSlot::Main) |
                actionButtonBit(ActionButtonSlot::General3));
        const auto tradeHit = actionButtonHit(224, 420,
            ActionButtonLayout::Trading,
            actionButtonBit(ActionButtonSlot::Main) |
                actionButtonBit(ActionButtonSlot::General2) |
                actionButtonBit(ActionButtonSlot::General3));
        expect(generalHit == ActionButtonSlot::Main &&
                buyHit == ActionButtonSlot::Main &&
                taxHit == ActionButtonSlot::General3 &&
                tradeHit == ActionButtonSlot::General2,
            "action hit-test selects source slot in each layout mode");

        const auto ambiguousAllSlots = actionButtonHit(251, 455,
            ActionButtonLayout::BuyAuction);
        expect(ambiguousAllSlots == ActionButtonSlot::General1,
            "legacy scan order exposes the one-pixel BuyAuction overlap when General1 is visible");

        expect(!actionButtonHit(0, 0).has_value() &&
                !actionButtonHit(449, 420, ActionButtonLayout::Trading).has_value(),
            "action hit-test rejects outside and exclusive-right coordinates");
    }


    void testPropertyTitleLayout()
    {
        using namespace monopoly::ibar::layout;

        expect(propertyIndex(1) == 0 && propertyIndex(39) == 27 &&
               propertyIndex(0) == -1 && propertyIndex(40) == -1,
            "propconv maps Mediterranean..Boardwalk and rejects non-ownable squares");
        expect(propertyBarOrder(1) == 11 && propertyBarOrder(3) == 9 &&
               propertyBarOrder(39) == 30,
            "IBARPropertyBarOrder maps representative deeds exactly");

        const Rect mediterranean = propertyRect(1);
        const Rect baltic = propertyRect(3);
        const Rect boardwalk = propertyRect(39);
        expect(mediterranean.left == 180 && mediterranean.top == 515 &&
               mediterranean.right == 214 && mediterranean.bottom == 557 &&
               baltic.left == 190 && baltic.top == 495 &&
               boardwalk.left == 698 && boardwalk.top == 495,
            "property title rectangles reproduce UDIBar coordinate formulas");

        const PropertyMask medBaltic = propertyBit(1) | propertyBit(3);
        const auto overlap = propertyHit(200, 520, medBaltic);
        expect(overlap && *overlap == 3,
            "property hit-test scans square 41->0 so Baltic wins overlapping Mediterranean");
        expect(!propertyHit(200, 520, 0).has_value(),
            "property hit-test ignores titles outside the visible property mask");
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

    testActionButtonRects();

    testPropertyTitleLayout();

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
