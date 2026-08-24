#include "DataBanks.hpp"
#include "LegacyTextIds.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <type_traits>

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


    void testTextMessageIds()
    {
        using namespace monopoly::legacy_text;


        expect(NoMessage == 0, "TMN_NO_MESSAGE == 0");
        expect(ErrorNameInUse == 1, "TMN_ERROR_NAME_IN_USE == 1");
        expect(
            ErrorNoMorePlayers == 2,
            "TMN_ERROR_NO_MORE_PLAYERS == 2"
        );
        expect(
            ErrorNotYourPlayer == 3,
            "TMN_ERROR_NOT_YOUR_PLAYER == 3"
        );
        expect(
            ErrorFunctionNotImplemented == 4,
            "TMN_ERROR_FUNCTION_NOT_IMPLEMENTED == 4"
        );
        expect(ErrorWrongPhase == 5, "TMN_ERROR_WRONG_PHASE == 5");
        expect(ErrorWrongPlayer == 6, "TMN_ERROR_WRONG_PLAYER == 6");
        expect(
            ErrorNeedTwoPlayers == 7,
            "TMN_ERROR_NEED_TWO_PLAYERS == 7"
        );
    }


    void testLegacyGroupIds()
    {
        using monopoly::data::LegacyGroupId;
        using monopoly::data::legacyGroupValue;


        expect(
            legacyGroupValue(LegacyGroupId::Main) == 2,
            "DAT_MAIN == 2"
        );
        expect(
            legacyGroupValue(LegacyGroupId::Patterns) == 3,
            "DAT_PAT == 3"
        );
        expect(
            legacyGroupValue(LegacyGroupId::LanguageGraphics) == 5,
            "DAT_LANG2 == 5"
        );
        expect(
            legacyGroupValue(LegacyGroupId::Board) == 6,
            "DAT_BOARD == 6"
        );
        expect(
            legacyGroupValue(LegacyGroupId::Board2) == 7,
            "DAT_BOARD2 == 7"
        );
        expect(
            legacyGroupValue(LegacyGroupId::ThreeD) == 8,
            "DAT_3D == 8"
        );
        expect(
            legacyGroupValue(LegacyGroupId::LanguageText) == 9,
            "DAT_LANG == 9"
        );
        expect(
            legacyGroupValue(LegacyGroupId::LanguageDialog) == 10,
            "DAT_LANGDIALOG == 10"
        );
    }


    void testDataIds()
    {
        using namespace monopoly::data;


        expect(EmptyDataId == 0U, "LE_DATA_EmptyItem == 0");
        expect(isEmptyDataId(EmptyDataId), "empty DataId is detected");
        expect(
            !isEmptyDataId(packDataId(LegacyGroupId::Main, 0)),
            "group 2 tag 0 is not the empty DataId"
        );


        constexpr auto mainFirst =
            packDataId(LegacyGroupId::Main, 0x0000U);
        constexpr auto modelItem =
            packDataId(LegacyGroupId::ThreeD, 0x066EU);
        constexpr auto dialogLast =
            packDataId(LegacyGroupId::LanguageDialog, 0xFFFFU);


        expect(mainFirst == 0x00020000U, "pack DAT_MAIN tag 0");
        expect(modelItem == 0x0008066EU, "pack DAT_3D tag 0x066E");
        expect(
            dialogLast == 0x000AFFFFU,
            "pack DAT_LANGDIALOG tag 0xFFFF"
        );
        expect(dataGroup(modelItem) == 8U, "extract DataId group");
        expect(dataTag(modelItem) == 0x066EU, "extract DataId tag");
        expect(dataGroup(dialogLast) == 10U, "extract maximum-tag group");
        expect(dataTag(dialogLast) == 0xFFFFU, "extract maximum tag");
        expect(
            idWithGroupFromParent(
                packDataId(LegacyGroupId::Main, 0x1234U),
                packDataId(LegacyGroupId::Board, 0xABCDU)) ==
                0x00061234U,
            "LE_DATA_IdWithFileFromParent keeps child tag and parent group"
        );
    }


    void testCoreBanks()
    {
        using namespace monopoly::data;


        const auto& usa = coreBanks(BoardEdition::Usa);
        const auto& europe = coreBanks(BoardEdition::Europe);


        expect(usa.size() == CoreBankCount, "USA core bank count == 5");
        expect(
            europe.size() == CoreBankCount,
            "Europe core bank count == 5"
        );


        expect(
            usa[0].id == BankId::Main &&
                usa[0].group == LegacyGroupId::Main &&
                usa[0].legacyPath == "Dat_Mon/dat_main.dat",
            "USA main bank contract"
        );
        expect(
            usa[1].id == BankId::Patterns &&
                usa[1].group == LegacyGroupId::Patterns &&
                usa[1].legacyPath == "Dat_Mon/dat_pat.dat",
            "USA pattern bank contract"
        );
        expect(
            usa[2].id == BankId::Board &&
                usa[2].group == LegacyGroupId::Board &&
                usa[2].legacyPath == "Dat_Mon/dat_bord.dat",
            "USA board bank contract"
        );
        expect(
            usa[3].id == BankId::Board2 &&
                usa[3].group == LegacyGroupId::Board2 &&
                usa[3].legacyPath == "Dat_Mon/dat_brd2.dat",
            "USA secondary board bank contract"
        );
        expect(
            usa[4].id == BankId::ThreeD &&
                usa[4].group == LegacyGroupId::ThreeD &&
                usa[4].legacyPath == "Dat_Mon/dat_3d.dat",
            "USA 3D bank contract"
        );


        expect(
            europe[2].id == BankId::Board &&
                europe[2].group == LegacyGroupId::Board &&
                europe[2].legacyPath == "Dat_Mon/dat_borde.dat",
            "Europe board bank uses dat_borde.dat"
        );


        bool otherEuropeBanksMatch = true;

        for (std::size_t index = 0; index < usa.size(); ++index)
        {
            if (index == 2)
            {
                continue;
            }


            otherEuropeBanksMatch =
                otherEuropeBanksMatch &&
                europe[index].id == usa[index].id &&
                europe[index].group == usa[index].group &&
                europe[index].legacyPath == usa[index].legacyPath;
        }


        expect(
            otherEuropeBanksMatch,
            "USA and Europe differ only on the primary board path"
        );
        expect(
            legacyBanks().data() == usa.data(),
            "legacyBanks remains the USA compatibility alias"
        );
    }


    void testLanguageBanks()
    {
        using namespace monopoly::data;


        constexpr std::array<LanguageId, LanguageCount> expectedLanguages
        {{
            LanguageId::EnglishUs,
            LanguageId::EnglishUk,
            LanguageId::French,
            LanguageId::German,
            LanguageId::Spanish,
            LanguageId::Dutch,
            LanguageId::Swedish,
            LanguageId::Finnish,
            LanguageId::Danish,
            LanguageId::Norwegian
        }};

        constexpr std::array<std::string_view, LanguageCount> textPaths
        {{
            "Dat_Mon/dat_ln01.dat",
            "Dat_Mon/dat_ln02.dat",
            "Dat_Mon/dat_ln03.dat",
            "Dat_Mon/dat_ln04.dat",
            "Dat_Mon/dat_ln05.dat",
            "Dat_Mon/dat_ln06.dat",
            "Dat_Mon/dat_ln07.dat",
            "Dat_Mon/dat_ln08.dat",
            "Dat_Mon/dat_ln09.dat",
            "Dat_Mon/dat_ln10.dat"
        }};

        constexpr std::array<std::string_view, LanguageCount> graphicsPaths
        {{
            "Dat_Mon/dat_lm01.dat",
            "Dat_Mon/dat_lm02.dat",
            "Dat_Mon/dat_lm03.dat",
            "Dat_Mon/dat_lm04.dat",
            "Dat_Mon/dat_lm05.dat",
            "Dat_Mon/dat_lm06.dat",
            "Dat_Mon/dat_lm07.dat",
            "Dat_Mon/dat_lm08.dat",
            "Dat_Mon/dat_lm09.dat",
            "Dat_Mon/dat_lm10.dat"
        }};

        constexpr std::array<std::string_view, LanguageCount> dialogPaths
        {{
            "Dat_Mon/dat_lk01.dat",
            "Dat_Mon/dat_lk02.dat",
            "Dat_Mon/dat_lk03.dat",
            "Dat_Mon/dat_lk04.dat",
            "Dat_Mon/dat_lk05.dat",
            "Dat_Mon/dat_lk06.dat",
            "Dat_Mon/dat_lk07.dat",
            "Dat_Mon/dat_lk08.dat",
            "Dat_Mon/dat_lk09.dat",
            "Dat_Mon/dat_lk10.dat"
        }};


        const auto& triplets = languageBankTriplets();

        expect(
            triplets.size() == LanguageCount,
            "language bank triplet count == 10"
        );


        for (std::size_t index = 0; index < triplets.size(); ++index)
        {
            const auto& triplet = triplets[index];

            const bool exactContract =
                triplet.language == expectedLanguages[index] &&
                triplet.text.id == BankId::LanguageText &&
                triplet.text.group == LegacyGroupId::LanguageText &&
                triplet.text.legacyPath == textPaths[index] &&
                triplet.graphics.id == BankId::LanguageGraphics &&
                triplet.graphics.group ==
                    LegacyGroupId::LanguageGraphics &&
                triplet.graphics.legacyPath == graphicsPaths[index] &&
                triplet.dialog.id == BankId::LanguageDialog &&
                triplet.dialog.group == LegacyGroupId::LanguageDialog &&
                triplet.dialog.legacyPath == dialogPaths[index] &&
                findLanguageBankTriplet(expectedLanguages[index]) ==
                    &triplet;

            expect(exactContract, textPaths[index]);
        }


        expect(
            findLanguageBankTriplet(static_cast<LanguageId>(0)) == nullptr,
            "language id 0 is rejected"
        );
        expect(
            findLanguageBankTriplet(static_cast<LanguageId>(11)) == nullptr,
            "language id 11 is rejected"
        );
    }
}


static_assert(
    std::is_same_v<monopoly::data::DataId, std::uint32_t>
);
static_assert(
    std::is_same_v<monopoly::data::DataTag, std::uint16_t>
);
static_assert(
    monopoly::data::packDataId(
        monopoly::data::LegacyGroupId::Board,
        0x1234U
    ) == 0x00061234U
);
static_assert(
    monopoly::data::dataGroup(0x00061234U) == 6U
);
static_assert(
    monopoly::data::dataTag(0x00061234U) == 0x1234U
);

static_assert(
    monopoly::data::idWithGroupFromParent(
        0x00021234U,
        0x0006ABCDU) == 0x00061234U
);


int main()
{
    std::cout
        << "Monopoly legacy data contract tests\n"
        << "===================================\n";


    testTextMessageIds();
    testLegacyGroupIds();
    testDataIds();
    testCoreBanks();
    testLanguageBanks();


    std::cout << '\n';


    if (failures != 0)
    {
        std::cerr
            << failures
            << " legacy data contract test(s) failed.\n";

        return 1;
    }


    std::cout
        << "All legacy data contract tests passed.\n";


    return 0;
}
