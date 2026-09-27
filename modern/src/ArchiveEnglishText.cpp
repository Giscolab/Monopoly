#include "ArchiveAssembly.hpp"

#include <charconv>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace monopoly::data::assembly
{
    namespace
    {
        std::string readAscii(const std::filesystem::path& path)
        {
            if (std::filesystem::file_size(path) > 16U * 1024U * 1024U)
                throw std::runtime_error("text input exceeds 16 MiB: " + path.string());
            std::ifstream stream(path, std::ios::binary);
            if (!stream) throw std::runtime_error("cannot open " + path.string());
            std::string text{std::istreambuf_iterator<char>(stream), {}};
            if (stream.bad()) throw std::runtime_error("cannot read " + path.string());
            for (unsigned char c : text)
                if (c == 0 || c > 127)
                    throw std::runtime_error("explicit text encoding required for " + path.string());
            return text;
        }

        struct AtrReader
        {
            std::string_view text;
            std::size_t offset{};
            [[noreturn]] void fail(std::string message) const
            { throw std::runtime_error("ATR byte " + std::to_string(offset) + ": " + message); }
            void skip()
            {
                while (offset < text.size())
                {
                    if (text[offset] == ' ' || text[offset] == '\t' ||
                        text[offset] == '\r' || text[offset] == '\n') { ++offset; continue; }
                    if (text.substr(offset, 2) == "//")
                    {
                        const auto end = text.find('\n', offset + 2);
                        offset = end == std::string_view::npos ? text.size() : end + 1;
                    }
                    else if (text.substr(offset, 2) == "/*")
                    {
                        const auto end = text.find("*/", offset + 2);
                        if (end == std::string_view::npos) fail("unterminated comment");
                        offset = end + 2;
                    }
                    else break;
                }
            }
            std::string word()
            {
                skip();
                const auto start = offset;
                while (offset < text.size())
                {
                    const char c = text[offset];
                    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '.')) break;
                    ++offset;
                }
                if (start == offset) fail("expected a token");
                return std::string(text.substr(start, offset - start));
            }
            std::string message()
            {
                skip();
                if (offset == text.size() || text[offset++] != '{') fail("expected message brace");
                const auto start = offset;
                const auto end = text.find('}', start);
                if (end == std::string_view::npos) fail("unterminated message");
                const auto nested = text.find('{', start);
                if (nested != std::string_view::npos && nested < end) fail("nested message brace");
                offset = end + 1;
                // Do not trim, expand ^ placeholders or remove comments in messages.
                return std::string(text.substr(start, end - start));
            }
        };
        void u16(DataBytes& out, std::uint16_t value)
        {
            out.push_back(static_cast<std::byte>(value & 255U));
            out.push_back(static_cast<std::byte>(value >> 8U));
        }
        void u32(DataBytes& out, std::uint32_t value)
        { u16(out, static_cast<std::uint16_t>(value)); u16(out, static_cast<std::uint16_t>(value >> 16U)); }
    }

    EnglishText readEnglishText(const std::filesystem::path& definitions,
        const std::filesystem::path& atr)
    {
        EnglishText result;
        std::map<std::string, std::uint32_t> symbols;
        auto generated = readAscii(definitions);
        // dat_lang.h appends hand-written aliases, separators and enum bases.
        // They are not additional messages and must not become required IDs.
        const auto extras = generated.find("/* Extra stuff (not automatically generated)");
        if (extras != std::string::npos) generated.resize(extras);
        std::istringstream header(generated);
        std::string line;
        while (std::getline(header, line))
        {
            std::istringstream tokens(line);
            std::string define, name, number, trailing;
            if (!(tokens >> define) || define != "#define") continue;
            if (!(tokens >> name >> number))
                throw std::runtime_error("malformed language definition: " + line);
            std::uint32_t id{};
            const auto parsed = std::from_chars(number.data(), number.data() + number.size(), id);
            if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() ||
                ((tokens >> trailing) && !trailing.starts_with("//")))
                throw std::runtime_error("non-literal language definition: " + name);
            if (!symbols.emplace(name, id).second)
                throw std::runtime_error("duplicate language definition: " + name);
            result.requiredNames.try_emplace(id, name);
        }
        if (symbols.empty()) throw std::runtime_error("language definitions are empty");
        const auto source = readAscii(atr);
        AtrReader reader{source};
        if (reader.word() != "_32Bit" || reader.word() != "_32Bit" ||
            reader.word() != "_String" || reader.word() != "252")
            reader.fail("unsupported ATR schema; expected two 32-bit fields and String 252");
        std::set<std::string> seen;
        while (true)
        {
            reader.skip();
            if (reader.offset == source.size()) break;
            if (reader.word() != ".ITEM") reader.fail("expected .ITEM");
            const auto symbol = reader.word();
            const auto sound = reader.word();
            auto message = reader.message();
            if (!seen.insert(symbol).second) reader.fail("duplicate message " + symbol);
            if (sound != "WAV_NO_SOUND")
                reader.fail("audio mapping requires a separate supplied sound bank: " + sound);
            const auto found = symbols.find(symbol);
            if (found == symbols.end())
            {
                result.unmappedSymbols.push_back(symbol);
                continue;
            }
            const auto [item, inserted] = result.text.emplace(found->second, message);
            if (!inserted && item->second != message)
                reader.fail("conflicting aliases for language ID " + std::to_string(found->second));
        }
        return result;
    }

    std::vector<ArchiveBuildItem> buildEnglishItems(const EnglishText& input)
    {
        if (input.text.empty()) return {};
        if (input.text.size() >= LegacyDataArchive::MaximumItemCount)
            throw std::runtime_error("language strings exceed DataTag capacity");
        std::vector<ArchiveBuildItem> items(1);
        items[0].type = LegacyDataType::IndexTable;
        for (const auto& [id, message] : input.text)
        {
            u32(items[0].payload, id);
            u16(items[0].payload, static_cast<std::uint16_t>(items.size()));
            DataBytes text;
            for (unsigned char character : message) u16(text, character);
            u16(text, 0);
            items.push_back({LegacyDataType::String, std::move(text)});
        }
        return items;
    }
}
