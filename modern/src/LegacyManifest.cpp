#include "LegacyManifest.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace monopoly::data
{
    namespace
    {
        [[nodiscard]] ManifestError makeError(
            ManifestErrorCode code,
            const std::filesystem::path& path,
            std::size_t line,
            std::string detail)
        {
            return { code, path, line, std::move(detail) };
        }


        [[nodiscard]] std::string_view trim(std::string_view text) noexcept
        {
            constexpr std::string_view Whitespace = " \t\r\n";
            const auto first = text.find_first_not_of(Whitespace);

            if (first == std::string_view::npos)
            {
                return {};
            }

            return text.substr(
                first,
                text.find_last_not_of(Whitespace) - first + 1);
        }


        [[nodiscard]] std::optional<std::uint32_t> parseUnsigned(
            std::string_view text) noexcept
        {
            text = trim(text);
            int base = 10;

            if (text.size() > 2 && text[0] == '0' &&
                (text[1] == 'x' || text[1] == 'X'))
            {
                text.remove_prefix(2);
                base = 16;
            }

            if (text.empty())
            {
                return std::nullopt;
            }

            std::uint32_t value{};
            const auto result = std::from_chars(
                text.data(), text.data() + text.size(), value, base);

            if (result.ec != std::errc{} ||
                result.ptr != text.data() + text.size())
            {
                return std::nullopt;
            }

            return value;
        }


        [[nodiscard]] std::optional<LegacyDataType> typeFromSymbol(
            std::string_view symbol) noexcept
        {
            if (symbol.starts_with("BMP_"))
            {
                return LegacyDataType::Bitmap;
            }

            if (symbol.starts_with("CNK_"))
            {
                return LegacyDataType::Chunky;
            }

            if (symbol.starts_with("HMD_"))
            {
                return LegacyDataType::Hmd;
            }

            if (symbol.starts_with("TAB_"))
            {
                // Tableaux generiques du builder : mapping DataUAP prouve
                // dans la table de prefixes de L_Data.cpp.
                return LegacyDataType::Uap;
            }

            if (symbol.starts_with("WAV_"))
            {
                return LegacyDataType::Wave;
            }

            return std::nullopt;
        }


        [[nodiscard]] bool isSymbolCharacter(char value) noexcept
        {
            return
                (value >= 'a' && value <= 'z') ||
                (value >= 'A' && value <= 'Z') ||
                (value >= '0' && value <= '9') ||
                value == '_';
        }
    }


    std::string_view manifestErrorCodeName(
        ManifestErrorCode code) noexcept
    {
        switch (code)
        {
        case ManifestErrorCode::None: return "None";
        case ManifestErrorCode::FileOpenFailed: return "FileOpenFailed";
        case ManifestErrorCode::MissingGeneratorSignature:
            return "MissingGeneratorSignature";
        case ManifestErrorCode::MissingDeclaredCount:
            return "MissingDeclaredCount";
        case ManifestErrorCode::MalformedDefinition:
            return "MalformedDefinition";
        case ManifestErrorCode::UnknownItemPrefix: return "UnknownItemPrefix";
        case ManifestErrorCode::TagOutOfRange: return "TagOutOfRange";
        case ManifestErrorCode::DuplicateSymbol: return "DuplicateSymbol";
        case ManifestErrorCode::DuplicateTag: return "DuplicateTag";
        case ManifestErrorCode::NonContiguousTag: return "NonContiguousTag";
        case ManifestErrorCode::CountMismatch: return "CountMismatch";
        case ManifestErrorCode::OutputOpenFailed: return "OutputOpenFailed";
        case ManifestErrorCode::OutputWriteFailed: return "OutputWriteFailed";
        }

        return "InvalidManifestErrorCode";
    }


    std::expected<LegacyBankManifest, ManifestError> parseDmakeManifest(
        std::istream& input,
        std::string bankName,
        std::filesystem::path diagnosticPath)
    {
        LegacyBankManifest manifest;
        manifest.bankName = std::move(bankName);
        bool hasSignature = false;
        std::optional<std::uint32_t> declaredCount;
        std::unordered_set<std::string> symbols;
        std::unordered_set<DataTag> tags;
        std::string line;
        std::size_t lineNumber = 0;

        while (std::getline(input, line))
        {
            ++lineNumber;

            if (line.find("DMAKE99: Data file builder ver 2.03") !=
                std::string::npos)
            {
                hasSignature = true;
            }

            constexpr std::string_view CountPrefix = "There were ";
            constexpr std::string_view CountSuffix =
                " unique items in the DF file.";
            const auto countStart = line.find(CountPrefix);

            if (countStart != std::string::npos)
            {
                const auto digitsStart = countStart + CountPrefix.size();
                const auto countEnd = line.find(CountSuffix, digitsStart);

                if (countEnd != std::string::npos)
                {
                    declaredCount = parseUnsigned(std::string_view(line).substr(
                        digitsStart, countEnd - digitsStart));
                }
            }

            auto view = trim(line);

            if (!view.starts_with("#define"))
            {
                continue;
            }

            view.remove_prefix(std::string_view("#define").size());
            view = trim(view);
            const auto symbolEnd = std::find_if(
                view.begin(), view.end(),
                [](char value) { return !isSymbolCharacter(value); });
            const auto symbolLength = static_cast<std::size_t>(
                std::distance(view.begin(), symbolEnd));

            if (symbolLength == 0)
            {
                return std::unexpected(makeError(
                    ManifestErrorCode::MalformedDefinition,
                    diagnosticPath,
                    lineNumber,
                    "#define has no symbol"));
            }

            const std::string symbol(view.substr(0, symbolLength));
            const auto rawValue = trim(view.substr(symbolLength));

            if (symbol == "DEF_blank")
            {
                continue;
            }

            const auto type = typeFromSymbol(symbol);

            if (!type)
            {
                return std::unexpected(makeError(
                    ManifestErrorCode::UnknownItemPrefix,
                    diagnosticPath,
                    lineNumber,
                    "unsupported DMake item prefix in " + symbol));
            }

            const auto rawTag = parseUnsigned(rawValue);

            if (!rawTag)
            {
                return std::unexpected(makeError(
                    ManifestErrorCode::MalformedDefinition,
                    diagnosticPath,
                    lineNumber,
                    "item tag is not an unsigned decimal or hexadecimal value"));
            }

            if (*rawTag > std::numeric_limits<DataTag>::max())
            {
                return std::unexpected(makeError(
                    ManifestErrorCode::TagOutOfRange,
                    diagnosticPath,
                    lineNumber,
                    "DMake item tag exceeds the 16-bit DataTag space"));
            }

            const auto tag = static_cast<DataTag>(*rawTag);

            if (!symbols.emplace(symbol).second)
            {
                return std::unexpected(makeError(
                    ManifestErrorCode::DuplicateSymbol,
                    diagnosticPath,
                    lineNumber,
                    "duplicate DMake symbol " + symbol));
            }

            if (!tags.emplace(tag).second)
            {
                return std::unexpected(makeError(
                    ManifestErrorCode::DuplicateTag,
                    diagnosticPath,
                    lineNumber,
                    "duplicate DMake numeric tag"));
            }

            const auto expectedTag = manifest.entries.size();

            if (tag != expectedTag)
            {
                return std::unexpected(makeError(
                    ManifestErrorCode::NonContiguousTag,
                    diagnosticPath,
                    lineNumber,
                    "DMake tags must follow their source order from zero"));
            }

            manifest.entries.push_back({ tag, *type, symbol });
        }

        if (!hasSignature)
        {
            return std::unexpected(makeError(
                ManifestErrorCode::MissingGeneratorSignature,
                diagnosticPath,
                0,
                "header is not identified as DMAKE99 version 2.03 output"));
        }

        if (!declaredCount)
        {
            return std::unexpected(makeError(
                ManifestErrorCode::MissingDeclaredCount,
                diagnosticPath,
                0,
                "DMake declared item count was not found"));
        }

        if (*declaredCount != manifest.entries.size())
        {
            return std::unexpected(makeError(
                ManifestErrorCode::CountMismatch,
                diagnosticPath,
                0,
                "DMake declared item count differs from parsed definitions"));
        }

        manifest.declaredItemCount = *declaredCount;
        return manifest;
    }


    std::expected<LegacyBankManifest, ManifestError> readDmakeManifest(
        const std::filesystem::path& path)
    {
        std::ifstream input(path);

        if (!input)
        {
            return std::unexpected(makeError(
                ManifestErrorCode::FileOpenFailed,
                path,
                0,
                "unable to open generated DMake header"));
        }

        return parseDmakeManifest(input, path.stem().string(), path);
    }


    std::expected<void, ManifestError> writeManifestTsv(
        const std::filesystem::path& outputPath,
        std::span<const LegacyBankManifest> manifests)
    {
        std::ofstream output(outputPath, std::ios::trunc);

        if (!output)
        {
            return std::unexpected(makeError(
                ManifestErrorCode::OutputOpenFailed,
                outputPath,
                0,
                "unable to create manifest output"));
        }

        output << "bank\ttag\ttype\tsymbol\n";

        for (const auto& manifest : manifests)
        {
            for (const auto& entry : manifest.entries)
            {
                output
                    << manifest.bankName << '\t'
                    << entry.tag << '\t'
                    << legacyDataTypeName(entry.type) << '\t'
                    << entry.symbol << '\n';
            }
        }

        if (!output)
        {
            return std::unexpected(makeError(
                ManifestErrorCode::OutputWriteFailed,
                outputPath,
                0,
                "unable to finish manifest output"));
        }

        return {};
    }
}
