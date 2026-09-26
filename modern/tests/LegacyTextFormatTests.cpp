#include "LegacyTextFormat.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    using namespace monopoly;

    void require(bool condition, std::string_view description)
    {
        if (!condition)
            throw std::runtime_error(std::string(description));
    }

    language::LegacyFormatResolvers resolvers()
    {
        return {
            [](rules::PlayerNumber player)
                -> std::expected<std::u16string, std::string>
            {
                if (player == 2) return u"Player Two";
                if (player == rules::BankPlayer) return u"Bank";
                if (player == rules::NobodyPlayer) return u"Nobody";
                return u"Spectator";
            },
            [](std::int64_t square)
                -> std::expected<std::u16string, std::string>
            {
                return square == 7 ? std::u16string{u"Chance"} :
                    std::u16string{u"Square"};
            },
            [](std::uint32_t properties)
                -> std::expected<std::u16string, std::string>
            {
                return properties == 3 ? std::u16string{u"Boardwalk, Park Place"} :
                    std::u16string{u"Properties"};
            }
        };
    }

    void testControlExpansion()
    {
        const language::LegacyFormatArguments args{
            2, rules::BankPlayer, 7, 3, u"argument"};
        const auto text = language::formatLegacyMessage(
            u"1=^1 2=^2 3=^3 4=^4 A=^A P=^P Q=^Q S=^S T=^T",
            args, resolvers());
        require(text.has_value(), "all legacy format controls expand");
        require(*text ==
            u"1=2 2=6 3=7 4=3 A=argument P=Bank Q=Player Two "
            u"S=Chance T=Boardwalk, Park Place",
            "legacy controls use the original number/player/property mapping");
    }

    void testLegacyQuirksAndBounds()
    {
        language::LegacyFormatArguments args{};
        auto custom = resolvers();

        const auto unknown = language::formatLegacyMessage(
            u"before^zafter^", args, custom);
        require(unknown && *unknown == u"before^^after",
            "unknown controls preserve the original doubled-caret quirk and trailing caret stops");

        args.numberD = -1;
        const auto invalidSquare = language::formatLegacyMessage(
            u"^S", args, custom);
        require(invalidSquare && *invalidSquare == u"?",
            "invalid square formatting keeps the legacy question-mark fallback");

        std::u16string longArgument(3000, u'x');
        args.stringA = longArgument;
        const auto bounded = language::formatLegacyMessage(
            u"^A", args, custom);
        require(bounded &&
            bounded->size() == language::LegacyFormattedMessageLimit,
            "formatted notifications retain the 2047 UTF-16 code-unit retail limit");
    }

    void testWideConversion()
    {
        const auto text = language::wideToUtf16(L"Monopoly U0001F3B2");
        require(text && *text == u"Monopoly U0001F3B2",
            "wide player names convert to portable UTF-16 including supplementary characters");

        const std::wstring invalid{static_cast<wchar_t>(0xD800)};
        require(!language::wideToUtf16(invalid),
            "invalid wide Unicode is rejected rather than copied into LANG text");
    }
}

int main()
{
    try
    {
        testControlExpansion();
        testLegacyQuirksAndBounds();
        testWideConversion();
        std::cout << "[PASS] legacy message formatting and Unicode conversion\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
