#include "LanguageResources.hpp"
#include "LegacyDataArchiveBuilder.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
    using namespace monopoly::data;


    constexpr std::u16string_view MissingMessageLiteral =
        u" message not available.  "
        u"(1:^1, 2:^2, 3:^3, A:^A, P:^P, S:^S)";

    int failures = 0;


    void expect(bool condition, std::string_view description)
    {
        if (condition)
        {
            std::cout << "[PASS] " << description << '\n';
            return;
        }

        ++failures;
        std::cerr << "[FAIL] " << description << '\n';
    }


    class TemporaryDirectory final
    {
    public:
        explicit TemporaryDirectory(std::string_view suiteName)
        {
            const auto stamp = std::chrono::steady_clock::now()
                .time_since_epoch().count();
            const auto directoryName =
                std::string("MonopolyModern-") +
                std::string(suiteName) + "-" +
                std::to_string(stamp);

            std::error_code error;
            const auto tryCreate = [&] (const std::filesystem::path& root)
            {
                path_ = root / directoryName;
                error.clear();
                return std::filesystem::create_directories(path_, error) &&
                    !error;
            };

            std::error_code temporaryError;
            const auto temporaryRoot =
                std::filesystem::temp_directory_path(temporaryError);

            if (!temporaryError)
            {
                created_ = tryCreate(temporaryRoot);
            }

            if (!created_)
            {
                // Les runners confines peuvent exposer TEMP en lecture seule;
                // CTest garantit en revanche un repertoire de travail build.
                created_ = tryCreate(std::filesystem::current_path());
            }

            expect(
                created_ && !error,
                "temporary fixture directory is created"
            );
        }

        TemporaryDirectory(const TemporaryDirectory&) = delete;
        TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

        ~TemporaryDirectory()
        {
            if (!created_)
            {
                return;
            }

            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }

        [[nodiscard]] const std::filesystem::path& path() const noexcept
        {
            return path_;
        }

        [[nodiscard]] bool valid() const noexcept
        {
            return created_;
        }

    private:
        std::filesystem::path path_;
        bool created_{};
    };


    void appendU16Le(DataBytes& bytes, std::uint16_t value)
    {
        bytes.push_back(static_cast<std::byte>(value & 0xFFU));
        bytes.push_back(
            static_cast<std::byte>((value >> 8U) & 0xFFU));
    }


    void appendU32Le(DataBytes& bytes, std::uint32_t value)
    {
        bytes.push_back(static_cast<std::byte>(value & 0xFFU));
        bytes.push_back(
            static_cast<std::byte>((value >> 8U) & 0xFFU));
        bytes.push_back(
            static_cast<std::byte>((value >> 16U) & 0xFFU));
        bytes.push_back(
            static_cast<std::byte>((value >> 24U) & 0xFFU));
    }


    [[nodiscard]] DataBytes encodeCodeUnits(
        std::initializer_list<std::uint16_t> codeUnits)
    {
        DataBytes bytes;
        bytes.reserve(codeUnits.size() * 2U);

        for (const std::uint16_t codeUnit : codeUnits)
        {
            appendU16Le(bytes, codeUnit);
        }

        return bytes;
    }


    [[nodiscard]] DataBytes encodeUtf16Le(
        std::u16string_view text,
        std::size_t trailingNulls = 1)
    {
        DataBytes bytes;
        bytes.reserve((text.size() + trailingNulls) * 2U);

        for (const char16_t codeUnit : text)
        {
            appendU16Le(bytes, static_cast<std::uint16_t>(codeUnit));
        }

        for (std::size_t index = 0; index < trailingNulls; ++index)
        {
            appendU16Le(bytes, 0);
        }

        return bytes;
    }


    [[nodiscard]] DataBytes makeIndexBytes(
        std::initializer_list<DataIndexEntry> entries)
    {
        DataBytes bytes;
        bytes.reserve(entries.size() * DataIndexTable::PhysicalEntrySize);

        for (const auto [messageId, tag] : entries)
        {
            appendU32Le(bytes, messageId);
            appendU16Le(bytes, tag);
        }

        return bytes;
    }


    [[nodiscard]] std::vector<ArchiveBuildItem> makeValidTextItems()
    {
        std::vector<ArchiveBuildItem> items(4);
        items[0] =
        {
            LegacyDataType::IndexTable,
            makeIndexBytes(
                { { 0, 1 }, { 42, 2 }, { 43, 3 }, { 1'000, 2 } })
        };
        items[1] =
        {
            LegacyDataType::String,
            encodeUtf16Le(u" fallback ")
        };
        items[2] =
        {
            LegacyDataType::String,
            encodeUtf16Le(u"line 1\nline 2 \r\n  ")
        };
        items[3] =
        {
            LegacyDataType::String,
            encodeUtf16Le(u" \t\r\n  ")
        };
        return items;
    }


    [[nodiscard]] ArchiveOpenOptions verifiedOptions()
    {
        ArchiveOpenOptions options;
        options.checksumPolicy = ChecksumPolicy::Verify;
        return options;
    }


    [[nodiscard]] bool writeArchiveFixture(
        const std::filesystem::path& path,
        std::span<const ArchiveBuildItem> items)
    {
        std::error_code directoryError;
        std::filesystem::create_directories(
            path.parent_path(),
            directoryError);

        if (directoryError)
        {
            expect(false, "fixture archive parent directory is created");
            return false;
        }

        const auto result = writeLegacyDataArchive(path, items);
        expect(result.has_value(), "synthetic DAT fixture is written");
        return result.has_value();
    }


    [[nodiscard]] bool writeLanguageBank(
        const std::filesystem::path& root,
        const BankDefinition& definition,
        std::span<const ArchiveBuildItem> items)
    {
        return writeArchiveFixture(
            root / std::filesystem::path(
                std::string(definition.legacyPath)),
            items);
    }


    [[nodiscard]] std::expected<std::shared_ptr<LegacyDataArchive>, DataError>
    openArchive(
        const std::filesystem::path& path,
        std::uint16_t group)
    {
        return LegacyDataArchive::open(path, group, verifiedOptions());
    }


    void testUtf16LeDecoder()
    {
        const auto valid = decodeLegacyUtf16Le(encodeUtf16Le(
            u"A\u00E9\U0001F30D",
            3));
        expect(
            valid && *valid == u"A\u00E9\U0001F30D",
            "UTF-16LE decoder accepts BMP text, a surrogate pair and zero padding"
        );

        const auto empty = decodeLegacyUtf16Le(
            std::span<const std::byte>{});
        expect(
            !empty && empty.error().code == DataErrorCode::InvalidUtf16,
            "empty UTF-16 payload is rejected"
        );

        const DataBytes odd{ std::byte{ 0x41 } };
        const auto oddResult = decodeLegacyUtf16Le(odd);
        expect(
            !oddResult &&
                oddResult.error().code == DataErrorCode::InvalidUtf16,
            "odd-sized UTF-16 payload is rejected"
        );

        const auto noTerminator = decodeLegacyUtf16Le(
            encodeCodeUnits({ 0x0041, 0x0042 }));
        expect(
            !noTerminator &&
                noTerminator.error().code == DataErrorCode::InvalidUtf16,
            "UTF-16 payload without NUL terminator is rejected"
        );

        const auto trailingData = decodeLegacyUtf16Le(
            encodeCodeUnits({ 0x0041, 0x0000, 0x0042 }));
        expect(
            !trailingData &&
                trailingData.error().code == DataErrorCode::InvalidUtf16,
            "non-zero data after UTF-16 terminator is rejected"
        );

        const auto danglingHigh = decodeLegacyUtf16Le(
            encodeCodeUnits({ 0xD83C }));
        expect(
            !danglingHigh &&
                danglingHigh.error().code == DataErrorCode::InvalidUtf16,
            "dangling high surrogate is rejected"
        );

        const auto badPair = decodeLegacyUtf16Le(
            encodeCodeUnits({ 0xD83C, 0x0041, 0x0000 }));
        expect(
            !badPair &&
                badPair.error().code == DataErrorCode::InvalidUtf16,
            "high surrogate followed by a non-low code unit is rejected"
        );

        const auto loneLow = decodeLegacyUtf16Le(
            encodeCodeUnits({ 0xDF0D, 0x0000 }));
        expect(
            !loneLow &&
                loneLow.error().code == DataErrorCode::InvalidUtf16,
            "unpaired low surrogate is rejected"
        );
    }


    void testSparseLookupFallbackAndClean()
    {
        TemporaryDirectory temporary("LanguageCatalog");

        if (!temporary.valid())
        {
            return;
        }

        const auto path = temporary.path() / "sparse.dat";
        const auto items = makeValidTextItems();

        if (!writeArchiveFixture(path, items))
        {
            return;
        }

        const auto archive = openArchive(
            path,
            legacyGroupValue(LegacyGroupId::LanguageText));
        expect(archive.has_value(), "language text DAT opens");

        if (!archive)
        {
            return;
        }

        const auto catalog = LanguageCatalog::open(
            LanguageId::EnglishUs,
            *archive);
        expect(catalog.has_value(), "valid sparse language catalog opens");

        if (!catalog)
        {
            return;
        }

        expect(
            (*catalog)->language() == LanguageId::EnglishUs &&
                (*catalog)->maximumMessageId() == 1'000,
            "catalog retains language and largest sparse message ID"
        );

        const auto direct = (*catalog)->lookup(42);
        expect(
            direct && *direct && ***direct ==
                u"line 1\nline 2 \r\n  ",
            "sparse direct lookup decodes referenced DataString"
        );

        const auto directAgain = (*catalog)->lookup(42);
        expect(
            direct && *direct && directAgain && *directAgain &&
                (**direct).get() == (**directAgain).get(),
            "repeated lookup reuses owned decoded-text cache"
        );

        const auto alias = (*catalog)->lookup(1'000);
        expect(
            direct && *direct && alias && *alias &&
                ***alias == ***direct,
            "distinct sparse IDs may reference the same DAT tag"
        );

        const auto absent = (*catalog)->lookup(7);
        expect(
            absent && !*absent,
            "missing sparse ID is a normal empty optional lookup"
        );

        const auto directMessage = (*catalog)->message(42);
        expect(
            directMessage && **directMessage ==
                u"line 1\nline 2 \r\n  ",
            "direct message is returned without numeric prefix"
        );

        const auto fallback = (*catalog)->message(7);
        expect(
            fallback && **fallback == u"#7 fallback ",
            "missing non-zero ID prefixes decimal ID then uses message 0"
        );

        const auto cleaned = (*catalog)->cleanMessage(42);
        expect(
            cleaned && **cleaned == u"line 1\nline 2",
            "clean removes only trailing spaces and control characters"
        );

        const auto entirelyBlank = (*catalog)->cleanMessage(43);
        expect(
            entirelyBlank && **entirelyBlank == u" ",
            "clean preserves the first code unit of an entirely blank message"
        );
    }


    void testLiteralFallbackWithoutMessageZero()
    {
        TemporaryDirectory temporary("LanguageFallback");

        if (!temporary.valid())
        {
            return;
        }

        std::vector<ArchiveBuildItem> items(2);
        items[0] =
        {
            LegacyDataType::IndexTable,
            makeIndexBytes({ { 42, 1 } })
        };
        items[1] =
        {
            LegacyDataType::String,
            encodeUtf16Le(u"answer")
        };

        const auto path = temporary.path() / "literal.dat";

        if (!writeArchiveFixture(path, items))
        {
            return;
        }

        const auto archive = openArchive(
            path,
            legacyGroupValue(LegacyGroupId::LanguageText));

        if (!archive)
        {
            expect(false, "literal-fallback archive opens");
            return;
        }

        const auto catalog = LanguageCatalog::open(
            LanguageId::EnglishUs,
            *archive);

        if (!catalog)
        {
            expect(false, "literal-fallback catalog opens");
            return;
        }

        const auto direct = (*catalog)->message(42);
        expect(
            direct && **direct == u"answer",
            "present message remains direct when message 0 is absent"
        );

        const auto missing = (*catalog)->message(7);
        std::u16string expectedMissing = u"#7";
        expectedMissing.append(MissingMessageLiteral);
        expect(
            missing && **missing == expectedMissing,
            "missing ID uses exact legacy literal when message 0 is absent"
        );

        const auto zero = (*catalog)->message(0);
        std::u16string expectedZero = u"#0";
        expectedZero.append(MissingMessageLiteral);
        expect(
            zero && **zero == expectedZero,
            "missing message 0 uses #0 plus exact legacy literal"
        );
    }


    void testCatalogValidation()
    {
        TemporaryDirectory temporary("LanguageValidation");

        if (!temporary.valid())
        {
            return;
        }

        const auto runCase = [&temporary](
            std::string_view fileName,
            std::vector<ArchiveBuildItem> items,
            DataErrorCode expectedCode)
        {
            const auto path = temporary.path() /
                (std::string(fileName) + ".dat");

            if (!writeArchiveFixture(path, items))
            {
                return;
            }

            const auto archive = openArchive(
                path,
                legacyGroupValue(LegacyGroupId::LanguageText));

            if (!archive)
            {
                expect(false, "validation fixture archive opens");
                return;
            }

            const auto catalog = LanguageCatalog::open(
                LanguageId::EnglishUs,
                *archive);
            expect(
                !catalog && catalog.error().code == expectedCode,
                fileName
            );
        };

        runCase(
            "tag 0 must be IndexTable",
            {
                { LegacyDataType::String, encodeUtf16Le(u"not index") }
            },
            DataErrorCode::TypeMismatch);

        runCase(
            "index size must be a non-zero multiple of six",
            {
                { LegacyDataType::IndexTable,
                    DataBytes{ std::byte{ 1 } } },
                { LegacyDataType::String, encodeUtf16Le(u"text") }
            },
            DataErrorCode::InvalidIndexTable);

        runCase(
            "index keys must be sorted",
            {
                { LegacyDataType::IndexTable,
                    makeIndexBytes({ { 42, 1 }, { 7, 1 } }) },
                { LegacyDataType::String, encodeUtf16Le(u"text") }
            },
            DataErrorCode::UnsortedIndexTable);

        runCase(
            "duplicate index keys are rejected",
            {
                { LegacyDataType::IndexTable,
                    makeIndexBytes({ { 7, 1 }, { 7, 1 } }) },
                { LegacyDataType::String, encodeUtf16Le(u"text") }
            },
            DataErrorCode::DuplicateIndexKey);

        runCase(
            "index reference must remain inside DAT tag range",
            {
                { LegacyDataType::IndexTable,
                    makeIndexBytes({ { 0, 5 } }) },
                { LegacyDataType::String, encodeUtf16Le(u"text") }
            },
            DataErrorCode::TagOutOfRange);

        runCase(
            "index target must be DataString",
            {
                { LegacyDataType::IndexTable,
                    makeIndexBytes({ { 0, 1 } }) },
                { LegacyDataType::Bitmap,
                    DataBytes{ std::byte{ 0x42 } } }
            },
            DataErrorCode::TypeMismatch);

        const auto invalidStringPath =
            temporary.path() / "invalid-string.dat";
        const std::vector<ArchiveBuildItem> invalidStringItems
        {
            {
                LegacyDataType::IndexTable,
                makeIndexBytes({ { 42, 1 } })
            },
            {
                LegacyDataType::String,
                encodeCodeUnits({ 0x0041 })
            }
        };

        if (writeArchiveFixture(invalidStringPath, invalidStringItems))
        {
            const auto invalidStringArchive = openArchive(
                invalidStringPath,
                legacyGroupValue(LegacyGroupId::LanguageText));

            if (invalidStringArchive)
            {
                const auto invalidStringCatalog = LanguageCatalog::open(
                    LanguageId::EnglishUs,
                    *invalidStringArchive);

                if (invalidStringCatalog)
                {
                    const auto invalidString =
                        (*invalidStringCatalog)->lookup(42);
                    expect(
                        !invalidString && invalidString.error().code ==
                            DataErrorCode::InvalidUtf16 &&
                            invalidString.error().path == invalidStringPath &&
                            invalidString.error().tag == DataTag{ 1 },
                        "lookup annotates invalid UTF-16 with DAT path and tag"
                    );
                }
                else
                {
                    expect(false, "invalid-string catalog opens lazily");
                }
            }
            else
            {
                expect(false, "invalid-string archive opens");
            }
        }

        const auto validPath = temporary.path() / "language-id.dat";
        const auto validItems = makeValidTextItems();

        if (!writeArchiveFixture(validPath, validItems))
        {
            return;
        }

        const auto validArchive = openArchive(
            validPath,
            legacyGroupValue(LegacyGroupId::LanguageText));

        if (!validArchive)
        {
            expect(false, "valid archive for catalog guard tests opens");
            return;
        }

        const auto invalidLanguage = LanguageCatalog::open(
            static_cast<LanguageId>(0),
            *validArchive);
        expect(
            !invalidLanguage && invalidLanguage.error().code ==
                DataErrorCode::InvalidLanguage,
            "catalog rejects language ID outside source range 1..10"
        );

        const auto absentArchive = LanguageCatalog::open(
            LanguageId::EnglishUs,
            nullptr);
        expect(
            !absentArchive && absentArchive.error().code ==
                DataErrorCode::ArchiveClosed,
            "catalog rejects absent text archive"
        );

        const auto wrongGroupArchive = openArchive(
            validPath,
            legacyGroupValue(LegacyGroupId::LanguageGraphics));

        if (wrongGroupArchive)
        {
            const auto wrongGroup = LanguageCatalog::open(
                LanguageId::EnglishUs,
                *wrongGroupArchive);
            expect(
                !wrongGroup && wrongGroup.error().code ==
                    DataErrorCode::InvalidGroup,
                "catalog requires DAT_LANG group 9"
            );
        }
        else
        {
            expect(false, "wrong-group fixture archive opens");
        }

        (*validArchive)->close();
        const auto closed = LanguageCatalog::open(
            LanguageId::EnglishUs,
            *validArchive);
        expect(
            !closed && closed.error().code == DataErrorCode::ArchiveClosed,
            "catalog rejects an explicitly closed text archive"
        );
    }


    void testTransactionalLanguageSelection()
    {
        TemporaryDirectory temporary("LanguageService");

        if (!temporary.valid())
        {
            return;
        }

        const auto* englishDefinitions =
            findLanguageBankTriplet(LanguageId::EnglishUs);
        const auto* frenchDefinitions =
            findLanguageBankTriplet(LanguageId::French);

        if (englishDefinitions == nullptr || frenchDefinitions == nullptr)
        {
            expect(false, "language bank triplets are available");
            return;
        }

        const auto textItems = makeValidTextItems();
        const std::vector<ArchiveBuildItem> mediaItems
        {
            { LegacyDataType::Bitmap, DataBytes{ std::byte{ 0x4D } } }
        };
        const std::vector<ArchiveBuildItem> dialogItems
        {
            { LegacyDataType::Wave, DataBytes{ std::byte{ 0x44 } } }
        };

        if (!writeLanguageBank(
                temporary.path(),
                englishDefinitions->text,
                textItems) ||
            !writeLanguageBank(
                temporary.path(),
                englishDefinitions->graphics,
                mediaItems) ||
            !writeLanguageBank(
                temporary.path(),
                englishDefinitions->dialog,
                dialogItems))
        {
            return;
        }

        LanguageService service;
        const auto englishSelection = service.select(
            temporary.path(),
            LanguageId::EnglishUs,
            verifiedOptions());
        expect(
            englishSelection.has_value(),
            "service publishes language only after all three banks validate"
        );

        auto englishSnapshot = service.snapshot();

        if (!englishSelection || !englishSnapshot)
        {
            return;
        }

        expect(
            englishSnapshot->language == LanguageId::EnglishUs &&
                englishSnapshot->textArchive->group() == 9 &&
                englishSnapshot->mediaArchive->group() == 5 &&
                englishSnapshot->dialogArchive->group() == 10,
            "published snapshot owns the text, media and dialog triplet"
        );

        const auto heldEnglishText = englishSnapshot->catalog->message(42);

        if (!heldEnglishText)
        {
            expect(false, "owned English text is available before switch");
            return;
        }

        if (!writeLanguageBank(
                temporary.path(),
                frenchDefinitions->text,
                textItems) ||
            !writeLanguageBank(
                temporary.path(),
                frenchDefinitions->graphics,
                mediaItems))
        {
            return;
        }

        const auto incompleteFrench = service.select(
            temporary.path(),
            LanguageId::French,
            verifiedOptions());
        expect(
            !incompleteFrench && incompleteFrench.error().code ==
                DataErrorCode::FileOpenFailed,
            "selection fails when the third language bank is absent"
        );
        expect(
            service.snapshot().get() == englishSnapshot.get(),
            "failed staged selection preserves active snapshot exactly"
        );

        const auto invalidLanguage = service.select(
            temporary.path(),
            static_cast<LanguageId>(11),
            verifiedOptions());
        expect(
            !invalidLanguage && invalidLanguage.error().code ==
                DataErrorCode::InvalidLanguage &&
                service.snapshot().get() == englishSnapshot.get(),
            "invalid language selection also leaves active snapshot intact"
        );

        if (!writeLanguageBank(
                temporary.path(),
                frenchDefinitions->dialog,
                dialogItems))
        {
            return;
        }

        const auto frenchSelection = service.select(
            temporary.path(),
            LanguageId::French,
            verifiedOptions());
        const auto frenchSnapshot = service.snapshot();
        expect(
            frenchSelection.has_value() && frenchSnapshot &&
                frenchSnapshot->language == LanguageId::French &&
                frenchSnapshot.get() != englishSnapshot.get(),
            "complete staged triplet atomically replaces active language"
        );

        const auto oldMessageAfterSwitch =
            englishSnapshot->catalog->message(42);
        expect(
            oldMessageAfterSwitch &&
                **oldMessageAfterSwitch == **heldEnglishText,
            "caller-held old snapshot remains usable after replacement"
        );

        service.clear();
        expect(!service.snapshot(), "clear removes only the active snapshot");

        englishSnapshot.reset();
        expect(
            **heldEnglishText == u"line 1\nline 2 \r\n  ",
            "owned language string survives service and snapshot release"
        );
    }
}


int main()
{
    std::cout
        << "Monopoly language resource tests\n"
        << "================================\n";

    testUtf16LeDecoder();
    testSparseLookupFallbackAndClean();
    testLiteralFallbackWithoutMessageZero();
    testCatalogValidation();
    testTransactionalLanguageSelection();

    std::cout << '\n';

    if (failures != 0)
    {
        std::cerr << failures << " language resource test(s) failed.\n";
        return 1;
    }

    std::cout << "All language resource tests passed.\n";
    return 0;
}
