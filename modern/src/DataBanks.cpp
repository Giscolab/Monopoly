#include "DataBanks.hpp"

namespace monopoly::data
{
    namespace
    {
        constexpr CoreBankDefinitions UsaCoreBanks
        {{
            {
                BankId::Main,
                LegacyGroupId::Main,
                "Dat_Mon/dat_main.dat"
            },
            {
                BankId::Patterns,
                LegacyGroupId::Patterns,
                "Dat_Mon/dat_pat.dat"
            },
            {
                BankId::Board,
                LegacyGroupId::Board,
                "Dat_Mon/dat_bord.dat"
            },
            {
                BankId::Board2,
                LegacyGroupId::Board2,
                "Dat_Mon/dat_brd2.dat"
            },
            {
                BankId::ThreeD,
                LegacyGroupId::ThreeD,
                "Dat_Mon/dat_3d.dat"
            }
        }};


        constexpr CoreBankDefinitions EuropeCoreBanks
        {{
            {
                BankId::Main,
                LegacyGroupId::Main,
                "Dat_Mon/dat_main.dat"
            },
            {
                BankId::Patterns,
                LegacyGroupId::Patterns,
                "Dat_Mon/dat_pat.dat"
            },
            {
                BankId::Board,
                LegacyGroupId::Board,
                "Dat_Mon/dat_borde.dat"
            },
            {
                BankId::Board2,
                LegacyGroupId::Board2,
                "Dat_Mon/dat_brd2.dat"
            },
            {
                BankId::ThreeD,
                LegacyGroupId::ThreeD,
                "Dat_Mon/dat_3d.dat"
            }
        }};


        constexpr LanguageBankTriplets LanguageBanks
        {{
            {
                LanguageId::EnglishUs,
                { BankId::LanguageText, LegacyGroupId::LanguageText,
                    "Dat_Mon/dat_ln01.dat" },
                { BankId::LanguageGraphics, LegacyGroupId::LanguageGraphics,
                    "Dat_Mon/dat_lm01.dat" },
                { BankId::LanguageDialog, LegacyGroupId::LanguageDialog,
                    "Dat_Mon/dat_lk01.dat" }
            },
            {
                LanguageId::EnglishUk,
                { BankId::LanguageText, LegacyGroupId::LanguageText,
                    "Dat_Mon/dat_ln02.dat" },
                { BankId::LanguageGraphics, LegacyGroupId::LanguageGraphics,
                    "Dat_Mon/dat_lm02.dat" },
                { BankId::LanguageDialog, LegacyGroupId::LanguageDialog,
                    "Dat_Mon/dat_lk02.dat" }
            },
            {
                LanguageId::French,
                { BankId::LanguageText, LegacyGroupId::LanguageText,
                    "Dat_Mon/dat_ln03.dat" },
                { BankId::LanguageGraphics, LegacyGroupId::LanguageGraphics,
                    "Dat_Mon/dat_lm03.dat" },
                { BankId::LanguageDialog, LegacyGroupId::LanguageDialog,
                    "Dat_Mon/dat_lk03.dat" }
            },
            {
                LanguageId::German,
                { BankId::LanguageText, LegacyGroupId::LanguageText,
                    "Dat_Mon/dat_ln04.dat" },
                { BankId::LanguageGraphics, LegacyGroupId::LanguageGraphics,
                    "Dat_Mon/dat_lm04.dat" },
                { BankId::LanguageDialog, LegacyGroupId::LanguageDialog,
                    "Dat_Mon/dat_lk04.dat" }
            },
            {
                LanguageId::Spanish,
                { BankId::LanguageText, LegacyGroupId::LanguageText,
                    "Dat_Mon/dat_ln05.dat" },
                { BankId::LanguageGraphics, LegacyGroupId::LanguageGraphics,
                    "Dat_Mon/dat_lm05.dat" },
                { BankId::LanguageDialog, LegacyGroupId::LanguageDialog,
                    "Dat_Mon/dat_lk05.dat" }
            },
            {
                LanguageId::Dutch,
                { BankId::LanguageText, LegacyGroupId::LanguageText,
                    "Dat_Mon/dat_ln06.dat" },
                { BankId::LanguageGraphics, LegacyGroupId::LanguageGraphics,
                    "Dat_Mon/dat_lm06.dat" },
                { BankId::LanguageDialog, LegacyGroupId::LanguageDialog,
                    "Dat_Mon/dat_lk06.dat" }
            },
            {
                LanguageId::Swedish,
                { BankId::LanguageText, LegacyGroupId::LanguageText,
                    "Dat_Mon/dat_ln07.dat" },
                { BankId::LanguageGraphics, LegacyGroupId::LanguageGraphics,
                    "Dat_Mon/dat_lm07.dat" },
                { BankId::LanguageDialog, LegacyGroupId::LanguageDialog,
                    "Dat_Mon/dat_lk07.dat" }
            },
            {
                LanguageId::Finnish,
                { BankId::LanguageText, LegacyGroupId::LanguageText,
                    "Dat_Mon/dat_ln08.dat" },
                { BankId::LanguageGraphics, LegacyGroupId::LanguageGraphics,
                    "Dat_Mon/dat_lm08.dat" },
                { BankId::LanguageDialog, LegacyGroupId::LanguageDialog,
                    "Dat_Mon/dat_lk08.dat" }
            },
            {
                LanguageId::Danish,
                { BankId::LanguageText, LegacyGroupId::LanguageText,
                    "Dat_Mon/dat_ln09.dat" },
                { BankId::LanguageGraphics, LegacyGroupId::LanguageGraphics,
                    "Dat_Mon/dat_lm09.dat" },
                { BankId::LanguageDialog, LegacyGroupId::LanguageDialog,
                    "Dat_Mon/dat_lk09.dat" }
            },
            {
                LanguageId::Norwegian,
                { BankId::LanguageText, LegacyGroupId::LanguageText,
                    "Dat_Mon/dat_ln10.dat" },
                { BankId::LanguageGraphics, LegacyGroupId::LanguageGraphics,
                    "Dat_Mon/dat_lm10.dat" },
                { BankId::LanguageDialog, LegacyGroupId::LanguageDialog,
                    "Dat_Mon/dat_lk10.dat" }
            }
        }};
    }


    const CoreBankDefinitions& coreBanks(
        BoardEdition edition) noexcept
    {
        if (edition == BoardEdition::Europe)
        {
            return EuropeCoreBanks;
        }


        return UsaCoreBanks;
    }


    const CoreBankDefinitions& legacyBanks() noexcept
    {
        return coreBanks(BoardEdition::Usa);
    }


    const LanguageBankTriplets& languageBankTriplets() noexcept
    {
        return LanguageBanks;
    }


    const LanguageBankTriplet* findLanguageBankTriplet(
        LanguageId language) noexcept
    {
        const auto legacyId =
            static_cast<std::uint8_t>(language);

        if (legacyId == 0 || legacyId > LanguageBanks.size())
        {
            return nullptr;
        }


        return &LanguageBanks[legacyId - 1];
    }
}
