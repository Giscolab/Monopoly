#pragma once

#include "RuleTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <string_view>

namespace monopoly::language
{
    inline constexpr std::size_t LegacyFormattedMessageLimit = 2047;

    struct LegacyFormatArguments
    {
        std::int64_t numberB{};
        std::int64_t numberC{};
        std::int64_t numberD{};
        std::int64_t numberE{};
        std::u16string_view stringA{};
    };

    struct LegacyFormatResolvers
    {
        std::function<std::expected<std::u16string, std::string>(
            rules::PlayerNumber)> playerName;
        std::function<std::expected<std::u16string, std::string>(
            std::int64_t)> squareName;
        std::function<std::expected<std::u16string, std::string>(
            std::uint32_t)> propertySetNames;
    };

    [[nodiscard]] std::expected<std::u16string, std::string>
        wideToUtf16(std::wstring_view text);

    [[nodiscard]] std::expected<std::u16string, std::string>
        formatLegacyMessage(
            std::u16string_view pattern,
            const LegacyFormatArguments& arguments,
            const LegacyFormatResolvers& resolvers,
            std::size_t limit = LegacyFormattedMessageLimit);
}
