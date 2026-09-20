#include "StatsAccountRuntime.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>

namespace monopoly::statsui
{
    namespace
    {
        constexpr std::size_t RecordBytes = 8 + 500 * 2;
        void put32(std::array<unsigned char, RecordBytes>& bytes, std::size_t at,
            std::uint32_t value)
        {
            for (unsigned i = 0; i < 4; ++i)
                bytes[at + i] = static_cast<unsigned char>(value >> (8 * i));
        }
        std::uint32_t get32(const std::array<unsigned char, RecordBytes>& bytes,
            std::size_t at)
        {
            std::uint32_t value{};
            for (unsigned i = 0; i < 4; ++i)
                value |= static_cast<std::uint32_t>(bytes[at + i]) << (8 * i);
            return value;
        }
    }

    std::expected<std::u16string, std::string> statsPropertyName(
        const data::LanguageCatalog& catalog, data::BoardEdition edition,
        int city, int square)
    {
        if (square < 0 || square >= static_cast<int>(rules::SquareCount))
            return std::unexpected("Stats property is outside the board");
        if (city == -1 && edition == data::BoardEdition::Europe)
            city = static_cast<int>(catalog.language()) - 2;
        const auto id = 1001ULL + 42ULL * static_cast<unsigned>(std::max(city, 0)) + square;
        if (id > std::numeric_limits<std::uint32_t>::max())
            return std::unexpected("Stats city name offset overflows LANG IDs");
        auto text = catalog.lookup(static_cast<std::uint32_t>(id));
        if (!text) return std::unexpected(text.error().detail);
        if (!*text) return std::unexpected("Stats property name is missing from LANG");
        if (!(**text)->empty() && (**text)->front() == u'*')
        {
            text = catalog.lookup(1001U + static_cast<unsigned>(square));
            if (!text) return std::unexpected(text.error().detail);
            if (!*text) return std::unexpected("Stats base property name is missing from LANG");
        }
        return ***text;
    }

    std::expected<void, std::string> AccountRuntime::open(std::filesystem::path path)
    {
        if (path.empty() || !path.is_absolute())
            return std::unexpected("account history requires an explicit absolute path");
        AccountState loaded;
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if (error) return std::unexpected(error.message());
        if (exists)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) return std::unexpected("cannot open account history");
            std::array<unsigned char, RecordBytes> bytes{};
            while (input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
            {
                const auto player = get32(bytes, 0);
                if (player >= rules::MaxPlayers)
                    return std::unexpected("account history contains an invalid player");
                AccountHistoryRow row{static_cast<rules::PlayerNumber>(player), get32(bytes, 4), {}};
                bool terminated = false;
                for (std::size_t i = 0; i < 500; ++i)
                {
                    const auto unit = static_cast<char16_t>(bytes[8 + i * 2] |
                        (static_cast<unsigned>(bytes[9 + i * 2]) << 8));
                    if (unit == 0) { terminated = true; break; }
                    row.description.push_back(unit);
                }
                if (!terminated) return std::unexpected("account history description is unterminated");
                loaded.turn = std::max(loaded.turn, row.turn);
                loaded.history.push_back(std::move(row));
            }
            if (!input.eof() || input.gcount() != 0)
                return std::unexpected("account history contains a truncated record");
        }
        path_ = std::move(path);
        state_ = std::move(loaded);
        return {};
    }

    std::expected<void, std::string> AccountRuntime::resetGame()
    {
        if (path_.empty()) return std::unexpected("account history path is not configured");
        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        if (!output) return std::unexpected("cannot reset account history");
        state_ = {};
        return {};
    }

    void AccountRuntime::scroll(int lines) noexcept
    {
        state_.scrollLines = static_cast<int>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(state_.scrollLines) + lines, 0,
            state_.scrollLimit));
    }

    void AccountRuntime::setScrollLimit(int lines) noexcept
    {
        state_.scrollLimit = std::max(lines, 0);
        state_.scrollLines = std::min(state_.scrollLines, state_.scrollLimit);
    }

    std::expected<void, std::string> AccountRuntime::processRuleMessage(
        const actions::Message& message, const rules::GameState& previous,
        rules::PlayerNumber iBarPlayer, int city, const data::ResourceSnapshot& resources)
    {
        if (message.action == actions::Type::NotifyStartTurn)
        {
            if (state_.turn == 0)
            {
                if (path_.empty()) return std::unexpected("account history path is not configured");
                std::ofstream output(path_, std::ios::binary | std::ios::trunc);
                if (!output) return std::unexpected("cannot reset account history for first turn");
                state_.history.clear();
                state_.scrollLines = state_.scrollLimit = 0;
            }
            if (state_.turn == std::numeric_limits<std::uint32_t>::max())
                return std::unexpected("account history turn overflow");
            ++state_.turn;
            return {};
        }
        if (message.action != actions::Type::NotifySquareOwnership &&
            message.action != actions::Type::NotifySquareMortgage &&
            message.action != actions::Type::NotifySquareHouses) return {};
        if (message.numberA < 0 || message.numberA >= rules::SquareCount)
            return std::unexpected("account history notification has invalid square");
        const int square = static_cast<int>(message.numberA);
        auto player = iBarPlayer;
        if (message.action == actions::Type::NotifySquareOwnership)
        {
            if (message.numberB < 0 || message.numberB >= rules::MaxPlayers) return {};
            player = static_cast<rules::PlayerNumber>(message.numberB);
        }
        if (player >= rules::MaxPlayers)
            return std::unexpected("account history notification has invalid player");
        const auto language = resources.language();
        if (!language || !language->catalog)
            return std::unexpected("account history has no LANG catalog");
        const auto edition = resources.context().board;
        AccountHistoryRow row{player, state_.turn, {}};
        if (message.action == actions::Type::NotifySquareHouses)
        {
            const auto before = previous.squares[static_cast<std::size_t>(square)].houses;
            const bool sold = before > message.numberB;
            const bool hotel = (sold ? before : message.numberB) == message.numberC;
            if (edition != data::BoardEdition::Usa)
                return std::unexpected("retail European house-history text IDs are absent from the source catalog");
            row.description = sold ? (hotel ? u"Sold a hotel. " : u"Sold a house. ") :
                (hotel ? u"Purchased a hotel. " : u"Purchased a house. ");
        }
        else
        {
            const auto property = statsPropertyName(*language->catalog, edition, city, square);
            if (!property) return std::unexpected(property.error());
            if (edition == data::BoardEdition::Usa)
            {
                row.description = message.action == actions::Type::NotifySquareOwnership ?
                    u"Purchased property " : message.numberB ? u"Mortgaged property " : u"Unmortgaged property ";
                row.description += *property + u". ";
            }
            else
            {
                if (message.action != actions::Type::NotifySquareOwnership)
                    return std::unexpected("retail European mortgage-history text IDs are absent from the source catalog");
                const auto pattern = language->catalog->lookup(3178);
                if (!pattern) return std::unexpected(pattern.error().detail);
                if (!*pattern) return std::unexpected("account purchase history is missing LANG ID 3178");
                for (std::size_t i = 0; i < (***pattern).size(); ++i)
                {
                    const auto unit = (***pattern)[i];
                    if (unit == u'^' && i + 1 < (***pattern).size() &&
                        ((***pattern)[i + 1] == u'S' || (***pattern)[i + 1] == u's'))
                    { row.description += *property; ++i; }
                    else row.description.push_back(unit);
                }
            }
        }
        if (path_.empty()) return std::unexpected("account history path is not configured");
        if (row.description.size() >= 500)
        {
            row.description.resize(499);
            if (row.description.back() >= 0xD800 && row.description.back() <= 0xDBFF)
                row.description.pop_back();
        }
        std::array<unsigned char, RecordBytes> bytes{};
        put32(bytes, 0, row.player);
        put32(bytes, 4, row.turn);
        for (std::size_t i = 0; i < row.description.size(); ++i)
        {
            bytes[8 + i * 2] = static_cast<unsigned char>(row.description[i]);
            bytes[9 + i * 2] = static_cast<unsigned char>(row.description[i] >> 8);
        }
        std::ofstream output(path_, std::ios::binary | std::ios::app);
        if (!output || !output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()))
            return std::unexpected("cannot append account history");
        output.flush();
        if (!output) return std::unexpected("cannot flush account history");
        state_.history.push_back(std::move(row));
        return {};
    }
}
