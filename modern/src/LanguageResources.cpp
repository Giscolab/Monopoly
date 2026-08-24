#include "LanguageResources.hpp"

#include <charconv>
#include <iterator>
#include <utility>

namespace monopoly::data
{
    namespace
    {
        constexpr std::u16string_view MissingMessageLiteral =
            u" message not available.  "
            u"(1:^1, 2:^2, 3:^3, A:^A, P:^P, S:^S)";


        [[nodiscard]] std::uint16_t readU16Le(
            const std::byte* bytes) noexcept
        {
            return
                static_cast<std::uint16_t>(
                    std::to_integer<std::uint8_t>(bytes[0])) |
                static_cast<std::uint16_t>(
                    std::to_integer<std::uint8_t>(bytes[1]) << 8U);
        }


        [[nodiscard]] DataError textError(
            DataErrorCode code,
            std::string detail,
            std::optional<DataTag> tag = std::nullopt)
        {
            return { code, {}, tag, std::move(detail) };
        }


        void appendDecimal(std::u16string& output, std::uint32_t value)
        {
            char buffer[16]{};
            const auto result = std::to_chars(
                std::begin(buffer),
                std::end(buffer),
                value);

            for (auto cursor = std::begin(buffer); cursor != result.ptr; ++cursor)
            {
                output.push_back(static_cast<char16_t>(*cursor));
            }
        }
    }


    std::expected<std::u16string, DataError> decodeLegacyUtf16Le(
        std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() % 2 != 0)
        {
            return std::unexpected(textError(
                DataErrorCode::InvalidUtf16,
                "legacy UTF-16LE payload must have a non-zero even size"));
        }

        std::u16string decoded;
        decoded.reserve(bytes.size() / 2);
        bool terminated = false;

        for (std::size_t offset = 0; offset < bytes.size(); offset += 2)
        {
            const char16_t codeUnit =
                static_cast<char16_t>(readU16Le(bytes.data() + offset));

            if (terminated)
            {
                if (codeUnit != u'\0')
                {
                    return std::unexpected(textError(
                        DataErrorCode::InvalidUtf16,
                        "non-zero data follows the UTF-16 terminator"));
                }

                continue;
            }

            if (codeUnit == u'\0')
            {
                terminated = true;
                continue;
            }

            if (codeUnit >= 0xD800U && codeUnit <= 0xDBFFU)
            {
                if (offset + 3 >= bytes.size())
                {
                    return std::unexpected(textError(
                        DataErrorCode::InvalidUtf16,
                        "high surrogate is not followed by a code unit"));
                }

                const char16_t low = static_cast<char16_t>(
                    readU16Le(bytes.data() + offset + 2));

                if (low < 0xDC00U || low > 0xDFFFU)
                {
                    return std::unexpected(textError(
                        DataErrorCode::InvalidUtf16,
                        "high surrogate is not followed by a low surrogate"));
                }

                decoded.push_back(codeUnit);
                decoded.push_back(low);
                offset += 2;
                continue;
            }

            if (codeUnit >= 0xDC00U && codeUnit <= 0xDFFFU)
            {
                return std::unexpected(textError(
                    DataErrorCode::InvalidUtf16,
                    "unpaired low surrogate in UTF-16 payload"));
            }

            decoded.push_back(codeUnit);
        }

        if (!terminated)
        {
            return std::unexpected(textError(
                DataErrorCode::InvalidUtf16,
                "legacy string payload has no UTF-16 NUL terminator"));
        }

        return decoded;
    }


    std::expected<std::shared_ptr<LanguageCatalog>, DataError>
    LanguageCatalog::open(
        LanguageId language,
        std::shared_ptr<LegacyDataArchive> textArchive)
    {
        if (findLanguageBankTriplet(language) == nullptr)
        {
            return std::unexpected(textError(
                DataErrorCode::InvalidLanguage,
                "language ID must be in the source-defined range 1..10"));
        }

        if (!textArchive || !textArchive->isOpen())
        {
            return std::unexpected(textError(
                DataErrorCode::ArchiveClosed,
                "language text archive is absent or closed"));
        }

        if (textArchive->group() !=
            legacyGroupValue(LegacyGroupId::LanguageText))
        {
            return std::unexpected(textError(
                DataErrorCode::InvalidGroup,
                "language text archive must use DAT_LANG group 9"));
        }

        auto indexMetadata = textArchive->metadata(0);

        if (!indexMetadata)
        {
            return std::unexpected(indexMetadata.error());
        }

        if (indexMetadata->type != LegacyDataType::IndexTable)
        {
            return std::unexpected(textError(
                DataErrorCode::TypeMismatch,
                "language item tag 0 must be an index table",
                0));
        }

        auto indexBytes = textArchive->load(0);

        if (!indexBytes)
        {
            return std::unexpected(indexBytes.error());
        }

        auto parsedIndex = DataIndexTable::parse(**indexBytes);

        if (!parsedIndex)
        {
            return std::unexpected(parsedIndex.error());
        }

        // L_Data ne validait ces references qu'au moment de l'usage. Le
        // snapshot moderne les valide avant publication transactionnelle.
        for (const auto& entry : parsedIndex->entries())
        {
            auto target = textArchive->metadata(entry.dataTag);

            if (!target)
            {
                auto error = target.error();
                error.detail =
                    "language index references a tag outside the DAT bank";
                return std::unexpected(std::move(error));
            }

            if (target->type != LegacyDataType::String)
            {
                return std::unexpected(textError(
                    DataErrorCode::TypeMismatch,
                    "language index target must be a DataString item",
                    entry.dataTag));
            }
        }

        auto catalog = std::shared_ptr<LanguageCatalog>(
            new LanguageCatalog());
        catalog->language_ = language;
        catalog->archive_ = std::move(textArchive);
        catalog->maximumMessageId_ =
            parsedIndex->entries().back().indexValue;
        catalog->index_ = std::move(*parsedIndex);
        return catalog;
    }


    LanguageId LanguageCatalog::language() const noexcept
    {
        return language_;
    }


    std::uint32_t LanguageCatalog::maximumMessageId() const noexcept
    {
        return maximumMessageId_;
    }


    std::expected<std::optional<SharedLanguageText>, DataError>
    LanguageCatalog::lookup(std::uint32_t messageId) const
    {
        std::scoped_lock lock(mutex_);

        if (const auto cached = cache_.find(messageId);
            cached != cache_.end())
        {
            return std::optional<SharedLanguageText>{ cached->second };
        }

        const auto tag = index_.find(messageId);

        if (!tag)
        {
            return std::optional<SharedLanguageText>{};
        }

        auto bytes = archive_->load(*tag);

        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }

        auto decoded = decodeLegacyUtf16Le(**bytes);

        if (!decoded)
        {
            auto error = decoded.error();
            error.path = archive_->path();
            error.tag = *tag;
            return std::unexpected(std::move(error));
        }

        auto text = std::make_shared<const std::u16string>(
            std::move(*decoded));
        cache_.emplace(messageId, text);
        return std::optional<SharedLanguageText>{ std::move(text) };
    }


    std::expected<SharedLanguageText, DataError>
    LanguageCatalog::message(std::uint32_t messageId) const
    {
        auto direct = lookup(messageId);

        if (!direct)
        {
            return std::unexpected(direct.error());
        }

        if (*direct)
        {
            return **direct;
        }

        std::u16string fallback;
        fallback.push_back(u'#');
        appendDecimal(fallback, messageId);

        if (messageId != 0)
        {
            auto defaultMessage = lookup(0);

            if (!defaultMessage)
            {
                return std::unexpected(defaultMessage.error());
            }

            if (*defaultMessage)
            {
                fallback.append(***defaultMessage);
            }
            else
            {
                fallback.append(MissingMessageLiteral);
            }
        }
        else
        {
            fallback.append(MissingMessageLiteral);
        }

        return std::make_shared<const std::u16string>(std::move(fallback));
    }


    std::expected<SharedLanguageText, DataError>
    LanguageCatalog::cleanMessage(std::uint32_t messageId) const
    {
        auto source = message(messageId);

        if (!source)
        {
            return std::unexpected(source.error());
        }

        auto cleaned = std::make_shared<std::u16string>(**source);

        while (!cleaned->empty())
        {
            const char16_t character = cleaned->back();

            if (character >= 32 && character != u' ')
            {
                break;
            }

            cleaned->pop_back();
        }

        return std::const_pointer_cast<const std::u16string>(cleaned);
    }


    std::expected<void, DataError> LanguageService::select(
        const std::filesystem::path& legacyRoot,
        LanguageId language,
        ArchiveOpenOptions options)
    {
        const auto* definitions = findLanguageBankTriplet(language);

        if (definitions == nullptr)
        {
            return std::unexpected(textError(
                DataErrorCode::InvalidLanguage,
                "language ID must be in the source-defined range 1..10"));
        }

        auto text = LegacyDataArchive::open(
            legacyRoot / std::filesystem::path(definitions->text.legacyPath),
            legacyGroupValue(definitions->text.group),
            options);

        if (!text)
        {
            return std::unexpected(text.error());
        }

        auto media = LegacyDataArchive::open(
            legacyRoot /
                std::filesystem::path(definitions->graphics.legacyPath),
            legacyGroupValue(definitions->graphics.group),
            options);

        if (!media)
        {
            return std::unexpected(media.error());
        }

        auto dialog = LegacyDataArchive::open(
            legacyRoot /
                std::filesystem::path(definitions->dialog.legacyPath),
            legacyGroupValue(definitions->dialog.group),
            options);

        if (!dialog)
        {
            return std::unexpected(dialog.error());
        }

        auto catalog = LanguageCatalog::open(language, *text);

        if (!catalog)
        {
            return std::unexpected(catalog.error());
        }

        auto staged = std::make_shared<LanguageSnapshot>(LanguageSnapshot
        {
            language,
            *text,
            *media,
            *dialog,
            *catalog
        });

        std::scoped_lock lock(mutex_);
        active_ = std::move(staged);
        return {};
    }


    std::shared_ptr<const LanguageSnapshot>
    LanguageService::snapshot() const noexcept
    {
        std::scoped_lock lock(mutex_);
        return active_;
    }


    void LanguageService::clear() noexcept
    {
        std::scoped_lock lock(mutex_);
        active_.reset();
    }
}
