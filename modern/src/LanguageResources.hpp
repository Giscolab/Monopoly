#pragma once

#include "DataBanks.hpp"
#include "LegacyDataArchive.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>

namespace monopoly::data
{
    using SharedLanguageText = std::shared_ptr<const std::u16string>;


    [[nodiscard]] std::expected<std::u16string, DataError>
    decodeLegacyUtf16Le(std::span<const std::byte> bytes);


    class LanguageCatalog final
    {
    public:
        [[nodiscard]] static std::expected<
            std::shared_ptr<LanguageCatalog>,
            DataError>
        open(
            LanguageId language,
            std::shared_ptr<LegacyDataArchive> textArchive);

        [[nodiscard]] LanguageId language() const noexcept;
        [[nodiscard]] std::uint32_t maximumMessageId() const noexcept;

        // Equivalent possede et nullable de GetLanguageString /
        // LANG_GetTextMessage2. Une absence normale vaut optional vide.
        [[nodiscard]] std::expected<
            std::optional<SharedLanguageText>,
            DataError>
        lookup(std::uint32_t messageId) const;

        // Fallback exact de LANG_GetTextMessage : ID direct, puis ID 0 dans
        // la meme banque, puis literal source; prefixe #<id> seulement lors
        // du fallback.
        [[nodiscard]] std::expected<SharedLanguageText, DataError>
        message(std::uint32_t messageId) const;

        // Conforme au code reel : seuls espaces U+0020 et controles finaux
        // sont retires; les retours a la ligne internes sont conserves. Une
        // chaine entierement blanche conserve son premier code unit.
        [[nodiscard]] std::expected<SharedLanguageText, DataError>
        cleanMessage(std::uint32_t messageId) const;

    private:
        LanguageCatalog() = default;

        LanguageId language_{ LanguageId::EnglishUs };
        std::shared_ptr<LegacyDataArchive> archive_;
        DataIndexTable index_;
        std::uint32_t maximumMessageId_{};
        mutable std::mutex mutex_;
        mutable std::unordered_map<std::uint32_t, SharedLanguageText> cache_;
    };


    struct LanguageSnapshot
    {
        LanguageId language{ LanguageId::EnglishUs };
        std::shared_ptr<LegacyDataArchive> textArchive;
        std::shared_ptr<LegacyDataArchive> mediaArchive;
        std::shared_ptr<LegacyDataArchive> dialogArchive;
        std::shared_ptr<LanguageCatalog> catalog;
    };


    class LanguageService final
    {
    public:
        // Publication transactionnelle : les trois banques et l'index texte
        // sont valides avant le remplacement du snapshot actif.
        [[nodiscard]] std::expected<void, DataError> select(
            const std::filesystem::path& legacyRoot,
            LanguageId language,
            ArchiveOpenOptions options = {});

        [[nodiscard]] std::shared_ptr<const LanguageSnapshot>
        snapshot() const noexcept;

        void clear() noexcept;

    private:
        mutable std::mutex mutex_;
        std::shared_ptr<const LanguageSnapshot> active_;
    };
}
