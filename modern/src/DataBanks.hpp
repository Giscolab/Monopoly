#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace monopoly::data
{
    using DataId = std::uint32_t;
    using DataTag = std::uint16_t;

    inline constexpr DataId EmptyDataId = 0;


    // Valeurs de groupes utilisées par Source/monopoly/GameInc.h.
    enum class LegacyGroupId : std::uint16_t
    {
        Main = 2,
        Patterns = 3,
        LanguageGraphics = 5,
        Board = 6,
        Board2 = 7,
        ThreeD = 8,
        LanguageText = 9,
        LanguageDialog = 10
    };


    [[nodiscard]] constexpr std::uint16_t legacyGroupValue(
        LegacyGroupId group) noexcept
    {
        return static_cast<std::uint16_t>(group);
    }


    // Contrat LE_DATA_IdFromTag : groupe sur les 16 bits hauts,
    // tag de l'item sur les 16 bits bas.
    [[nodiscard]] constexpr DataId packDataId(
        std::uint16_t group,
        DataTag tag) noexcept
    {
        return
            (static_cast<DataId>(group) << 16U) |
            static_cast<DataId>(tag);
    }


    [[nodiscard]] constexpr DataId packDataId(
        LegacyGroupId group,
        DataTag tag) noexcept
    {
        return packDataId(legacyGroupValue(group), tag);
    }


    [[nodiscard]] constexpr std::uint16_t dataGroup(
        DataId id) noexcept
    {
        return static_cast<std::uint16_t>(id >> 16U);
    }


    [[nodiscard]] constexpr DataTag dataTag(
        DataId id) noexcept
    {
        return static_cast<DataTag>(id & 0xFFFFU);
    }


    // Contrat LE_DATA_IdWithFileFromParent : conserve le tag de l'enfant,
    // mais resout son groupe dans le meme fichier que le parent. Ce helper
    // est notamment utilise par le sequenceur historique pour ses references
    // indirectes.
    [[nodiscard]] constexpr DataId idWithGroupFromParent(
        DataId child,
        DataId parent) noexcept
    {
        return packDataId(dataGroup(parent), dataTag(child));
    }


    [[nodiscard]] constexpr bool isEmptyDataId(
        DataId id) noexcept
    {
        return id == EmptyDataId;
    }


    enum class BoardEdition
    {
        Usa,
        Europe
    };


    enum class BankId
    {
        Main,
        Patterns,
        Board,
        Board2,
        ThreeD,
        LanguageText,
        LanguageGraphics,
        LanguageDialog
    };


    struct BankDefinition
    {
        BankId id;
        LegacyGroupId group;
        std::string_view legacyPath;
    };


    inline constexpr std::size_t CoreBankCount = 5;
    using CoreBankDefinitions =
        std::array<BankDefinition, CoreBankCount>;


    [[nodiscard]] const CoreBankDefinitions& coreBanks(
        BoardEdition edition) noexcept;

    // Alias conservé pour le caller actuel, qui représente le build
    // historique USA_VERSION = 1.
    [[nodiscard]] const CoreBankDefinitions& legacyBanks() noexcept;


    enum class LanguageId : std::uint8_t
    {
        EnglishUs = 1,
        EnglishUk = 2,
        French = 3,
        German = 4,
        Spanish = 5,
        Dutch = 6,
        Swedish = 7,
        Finnish = 8,
        Danish = 9,
        Norwegian = 10
    };


    struct LanguageBankTriplet
    {
        LanguageId language;
        BankDefinition text;
        BankDefinition graphics;
        BankDefinition dialog;
    };


    inline constexpr std::size_t LanguageCount = 10;
    using LanguageBankTriplets =
        std::array<LanguageBankTriplet, LanguageCount>;


    [[nodiscard]] const LanguageBankTriplets&
        languageBankTriplets() noexcept;

    [[nodiscard]] const LanguageBankTriplet*
        findLanguageBankTriplet(LanguageId language) noexcept;
}
