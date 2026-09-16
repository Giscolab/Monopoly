#include "MoneyFormat.hpp"

#include <limits>
#include <string_view>

namespace monopoly::money
{
    namespace
    {
        [[nodiscard]] std::expected<std::int64_t, std::string> scaled(
            std::int64_t value, std::int64_t factor)
        {
            if (factor <= 0)
                return std::unexpected("money scale factor is invalid");
            if (value > 0 && value > std::numeric_limits<std::int64_t>::max() / factor)
                return std::unexpected("money value overflows while applying retail currency scale");
            if (value < 0 && value < std::numeric_limits<std::int64_t>::min() / factor)
                return std::unexpected("money value underflows while applying retail currency scale");
            return value * factor;
        }

        [[nodiscard]] std::string decorated(
            std::int64_t value, bool printSymbol,
            std::string_view prefix, std::string_view suffix = {})
        {
            std::string result;
            if (printSymbol) result.append(prefix);
            result += std::to_string(value);
            if (printSymbol) result.append(suffix);
            return result;
        }
    }

    std::expected<std::string, std::string> format(
        std::int64_t defaultValue,
        int monetarySystem,
        bool printSymbol,
        data::BoardEdition edition)
    {
        // Udpenny.cpp is compiled with USA_VERSION for the USA edition and
        // unconditionally replaces the selected monetary system with US dollars.
        if (edition == data::BoardEdition::Usa)
            monetarySystem = 13;

        std::int64_t factor = 1;
        std::string_view prefix;
        std::string_view suffix;
        switch (monetarySystem)
        {
        case 0: prefix = "\xC2\xA3 "; break;          // UK pound
        case 1: factor = 100; prefix = "F "; break;    // French franc
        case 2: factor = 20; prefix = "DM "; break;    // Deutsche Mark
        case 3: factor = 100; break;                    // Spanish peseta: no symbol
        case 4: factor = 100; prefix = "f "; break;    // Dutch guilder
        case 5: factor = 20; prefix = "KR "; break;    // Swedish krona
        case 6: factor = 20; suffix = " MK"; break;    // Finnish markka
        case 7: factor = 20; suffix = " KR."; break;   // Danish krone
        case 8: factor = 20; prefix = "KR. "; break;   // Norwegian krone
        case 9: factor = 20; prefix = "F "; break;     // Belgian franc
        case 10: factor = 10; prefix = "$ "; break;   // Singapore dollar
        case 11: prefix = "$ "; break;                // Australian dollar
        case 12: prefix = "\xE2\x82\xAC "; break; // Euro
        case 13: prefix = "$ "; break;                // US dollar
        default:
            return std::unexpected("monetary system is outside retail 0..13 range");
        }

        const auto value = scaled(defaultValue, factor);
        if (!value) return std::unexpected(value.error());
        return decorated(*value, printSymbol, prefix, suffix);
    }
}
