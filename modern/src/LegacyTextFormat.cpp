#include "LegacyTextFormat.hpp"

#include <algorithm>
#include <limits>

namespace monopoly::language
{
    namespace
    {
        [[nodiscard]] char16_t upperAscii(char16_t value) noexcept
        {
            if (value >= u'a' && value <= u'z')
                return static_cast<char16_t>(value - (u'a' - u'A'));
            return value;
        }

        void appendBounded(
            std::u16string& output,
            std::u16string_view value,
            std::size_t limit)
        {
            if (output.size() >= limit || value.empty()) return;
            const auto available = limit - output.size();
            output.append(value.substr(0, available));
        }

        void appendNumber(
            std::u16string& output,
            std::int64_t value,
            std::size_t limit)
        {
            const auto ascii = std::to_string(value);
            if (output.size() >= limit) return;
            const auto count = std::min(ascii.size(), limit - output.size());
            for (std::size_t index = 0; index < count; ++index)
                output.push_back(static_cast<char16_t>(
                    static_cast<unsigned char>(ascii[index])));
        }

        [[nodiscard]] std::expected<std::u16string, std::string>
        resolvedPlayer(
            const LegacyFormatResolvers& resolvers,
            std::int64_t value)
        {
            if (value < 0 ||
                value > std::numeric_limits<rules::PlayerNumber>::max())
                return std::unexpected(
                    "legacy message player number is outside the portable range");
            if (!resolvers.playerName)
                return std::unexpected(
                    "legacy message requires a player-name resolver");
            return resolvers.playerName(
                static_cast<rules::PlayerNumber>(value));
        }
    }

    std::expected<std::u16string, std::string>
    wideToUtf16(std::wstring_view text)
    {
        std::u16string result;
        result.reserve(text.size());

        if constexpr (sizeof(wchar_t) == 2)
        {
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                const auto value =
                    static_cast<std::uint32_t>(text[index]);
                if (value >= 0xD800U && value <= 0xDBFFU)
                {
                    if (index + 1 >= text.size())
                        return std::unexpected(
                            "truncated UTF-16 surrogate pair");
                    const auto low =
                        static_cast<std::uint32_t>(text[index + 1]);
                    if (low < 0xDC00U || low > 0xDFFFU)
                        return std::unexpected(
                            "invalid UTF-16 surrogate pair");
                    result.push_back(static_cast<char16_t>(value));
                    result.push_back(static_cast<char16_t>(low));
                    ++index;
                    continue;
                }
                if (value >= 0xDC00U && value <= 0xDFFFU)
                    return std::unexpected(
                        "isolated UTF-16 low surrogate");
                result.push_back(static_cast<char16_t>(value));
            }
            return result;
        }

        for (const auto value : text)
        {
            const auto codePoint =
                static_cast<std::uint32_t>(value);
            if (codePoint > 0x10FFFFU ||
                (codePoint >= 0xD800U && codePoint <= 0xDFFFU))
                return std::unexpected(
                    "invalid wide-character Unicode code point");
            if (codePoint <= 0xFFFFU)
            {
                result.push_back(static_cast<char16_t>(codePoint));
                continue;
            }

            const auto adjusted = codePoint - 0x10000U;
            result.push_back(static_cast<char16_t>(
                0xD800U + (adjusted >> 10U)));
            result.push_back(static_cast<char16_t>(
                0xDC00U + (adjusted & 0x3FFU)));
        }
        return result;
    }

    std::expected<std::u16string, std::string>
    formatLegacyMessage(
        std::u16string_view pattern,
        const LegacyFormatArguments& arguments,
        const LegacyFormatResolvers& resolvers,
        std::size_t limit)
    {
        std::u16string output;
        output.reserve(std::min(pattern.size(), limit));

        for (std::size_t index = 0;
             index < pattern.size() && output.size() < limit;)
        {
            if (pattern[index] != u'^')
            {
                output.push_back(pattern[index++]);
                continue;
            }

            if (index + 1 >= pattern.size())
                break;

            const auto code = upperAscii(pattern[index + 1]);
            index += 2;

            switch (code)
            {
            case u'1':
                appendNumber(output, arguments.numberB, limit);
                break;
            case u'2':
                appendNumber(output, arguments.numberC, limit);
                break;
            case u'3':
                appendNumber(output, arguments.numberD, limit);
                break;
            case u'4':
                appendNumber(output, arguments.numberE, limit);
                break;
            case u'A':
                appendBounded(output, arguments.stringA, limit);
                break;
            case u'P':
            case u'Q':
            {
                const auto player = resolvedPlayer(
                    resolvers,
                    code == u'P' ? arguments.numberC : arguments.numberB);
                if (!player) return std::unexpected(player.error());
                appendBounded(output, *player, limit);
                break;
            }
            case u'S':
                if (arguments.numberD >= 0 &&
                    arguments.numberD <
                        static_cast<std::int64_t>(rules::SquareCount))
                {
                    if (!resolvers.squareName)
                        return std::unexpected(
                            "legacy message requires a square-name resolver");
                    const auto square =
                        resolvers.squareName(arguments.numberD);
                    if (!square) return std::unexpected(square.error());
                    appendBounded(output, *square, limit);
                }
                else
                {
                    appendBounded(output, u"?", limit);
                }
                break;
            case u'T':
            {
                if (arguments.numberE < 0 ||
                    static_cast<std::uint64_t>(arguments.numberE) >
                        std::numeric_limits<std::uint32_t>::max())
                    return std::unexpected(
                        "legacy property set is outside the 32-bit range");
                if (!resolvers.propertySetNames)
                    return std::unexpected(
                        "legacy message requires a property-set resolver");
                const auto properties = resolvers.propertySetNames(
                    static_cast<std::uint32_t>(arguments.numberE));
                if (!properties)
                    return std::unexpected(properties.error());
                appendBounded(output, *properties, limit);
                break;
            }
            default:
                // Userifce.cpp writes InputStringPntr (the '^') rather than
                // the unknown code into TempWString[1]. Preserve that quirk.
                appendBounded(output, u"^^", limit);
                break;
            }
        }

        return output;
    }
}
