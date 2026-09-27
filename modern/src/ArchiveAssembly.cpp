#include "ArchiveAssembly.hpp"
#include "LegacyBitmap.hpp"
#include "LegacyChunk.hpp"
#include "LegacyManifest.hpp"
#include "LegacyMeshData.hpp"
#include "LanguageResources.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace monopoly::data::assembly
{
    namespace
    {
        namespace fs = std::filesystem;
        constexpr std::uintmax_t PayloadLimit = 512ULL * 1024ULL * 1024ULL;
        struct Bank { const char* name; const char* header; std::uint16_t group; };
        constexpr std::array Banks{
            Bank{"dat_main", "dat_main.h", 2}, Bank{"dat_pat", "dat_pat.h", 3},
            Bank{"dat_bord", "dat_bord.h", 6}, Bank{"dat_brd2", "dat_brd2.h", 7},
            Bank{"dat_3d", "dat_3d.h", 8}, Bank{"dat_lm01", "dat_lm01.h", 5},
            Bank{"dat_lk01", "dat_lk01.h", 10}};

        std::string lower(std::string value)
        {
            for (char& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            return value;
        }
        std::string pathText(const fs::path& path)
        {
            const auto value = path.generic_u8string();
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }
        fs::path utf8Path(std::string_view value)
        { return fs::path(std::u8string(value.begin(), value.end())); }
        bool inside(const fs::path& parent, const fs::path& child)
        {
            auto p = parent.begin(), c = child.begin();
            for (; p != parent.end(); ++p, ++c)
            {
                if (c == child.end()) return false;
#ifdef _WIN32
                if (lower(pathText(*p)) != lower(pathText(*c))) return false;
#else
                if (*p != *c) return false;
#endif
            }
            return true;
        }
        fs::path resolvePath(const fs::path& path)
        { return fs::weakly_canonical(fs::absolute(path)); }
        void rejectOverlap(const fs::path& output, const fs::path& input)
        {
            if (inside(input, output) || inside(output, input))
                throw std::runtime_error("output and input trees must not overlap: " + pathText(input));
        }
        std::string cell(std::string value)
        {
            std::string out;
            for (char c : value)
            {
                if (c == '\t') out += "\\t";
                else if (c == '\r') out += "\\r";
                else if (c == '\n') out += "\\n";
                else if (c == '\\') out += "\\\\";
                else out += c;
            }
            return out;
        }
        std::ofstream report(const fs::path& path)
        {
            std::ofstream out(path, std::ios::binary);
            if (!out) throw std::runtime_error("cannot create " + pathText(path));
            out.exceptions(std::ios::badbit | std::ios::failbit);
            return out;
        }
        DataBytes readPayload(const fs::path& path)
        {
            const auto size = fs::file_size(path);
            if (size == 0 || size > PayloadLimit)
                throw std::runtime_error("payload must be nonempty and at most 512 MiB");
            DataBytes bytes(static_cast<std::size_t>(size));
            std::ifstream in(path, std::ios::binary);
            if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) ||
                in.peek() != std::char_traits<char>::eof())
                throw std::runtime_error("payload read failed or size changed: " + pathText(path));
            return bytes;
        }
        std::uint32_t u32(std::span<const std::byte> bytes, std::size_t n)
        {
            return std::to_integer<std::uint32_t>(bytes[n]) |
                (std::to_integer<std::uint32_t>(bytes[n+1]) << 8U) |
                (std::to_integer<std::uint32_t>(bytes[n+2]) << 16U) |
                (std::to_integer<std::uint32_t>(bytes[n+3]) << 24U);
        }
        bool magic(std::span<const std::byte> bytes, std::size_t n, std::string_view wanted)
        {
            if (n > bytes.size() || wanted.size() > bytes.size() - n) return false;
            for (std::size_t i = 0; i < wanted.size(); ++i)
                if (std::to_integer<unsigned char>(bytes[n+i]) != static_cast<unsigned char>(wanted[i])) return false;
            return true;
        }
        // Validate disk formats without loading SDL, decoding video, or inventing
        // absent content. Cross-bank CNK references are not a gameplay validation.
        void validate(LegacyDataType type, const DataBytes& bytes)
        {
            if (type == LegacyDataType::Bitmap)
            {
                const auto result = inspectLegacyBitmap(bytes);
                if (!result) throw std::runtime_error(result.error().detail);
            }
            else if (type == LegacyDataType::Uap)
            {
                const auto result = inspectLegacyUap(bytes);
                if (!result) throw std::runtime_error(result.error().detail);
            }
            else if (type == LegacyDataType::Chunky)
            {
                LegacyChunkReader reader{std::span<const std::byte>(bytes)};
                const auto first = reader.descend();
                if (!first) throw std::runtime_error(first.error().detail);
                if (first->id < 1 || first->id > 10 || first->endOffset != bytes.size())
                    throw std::runtime_error("CNK must contain one complete root sequence");
            }
            else if (type == LegacyDataType::Hmd)
            {
                const auto result = LegacyMeshData::parse(std::make_shared<const DataBytes>(bytes));
                if (!result) throw std::runtime_error(result.error().detail);
            }
            else if (type == LegacyDataType::Wave)
            {
                if (!magic(bytes, 0, "RIFF") || !magic(bytes, 8, "WAVE") || bytes.size() < 12)
                    throw std::runtime_error("WAV payload has no RIFF/WAVE header");
                const std::uint64_t end = 8ULL + u32(bytes, 4);
                if (end != bytes.size()) throw std::runtime_error("RIFF size does not match payload");
                bool format{}, data{};
                for (std::uint64_t p = 12; p < end;)
                {
                    if (end - p < 8) throw std::runtime_error("truncated RIFF chunk header");
                    const auto at = static_cast<std::size_t>(p);
                    const auto size = u32(bytes, at + 4);
                    const auto next = p + 8 + size + (size & 1U);
                    if (next > end) throw std::runtime_error("RIFF chunk exceeds payload");
                    if (magic(bytes, at, "fmt ")) format = size >= 16;
                    if (magic(bytes, at, "data")) data = size > 0;
                    p = next;
                }
                if (!format || !data) throw std::runtime_error("WAV is missing format or audio data");
            }
            else throw std::runtime_error("unsupported manifest payload type");
        }

        struct File { std::size_t root; fs::path relative; fs::path absolute; };
        using FileIndex = std::map<std::string, std::vector<File>>;
        FileIndex indexFiles(const std::vector<fs::path>& roots)
        {
            FileIndex result;
            for (std::size_t i = 0; i < roots.size(); ++i)
            {
                if (!fs::is_directory(roots[i]))
                    throw std::runtime_error("resource root is not a directory: " + pathText(roots[i]));
                for (const auto& entry : fs::recursive_directory_iterator(roots[i]))
                {
                    if (entry.is_symlink() || !entry.is_regular_file()) continue;
                    const auto absolute = resolvePath(entry.path());
                    if (!inside(roots[i], absolute)) continue;
                    const auto relative = absolute.lexically_relative(roots[i]);
                    result[lower(pathText(absolute.filename()))].push_back({i, relative, absolute});
                }
            }
            for (auto& [name, files] : result)
            {
                (void)name;
                std::ranges::sort(files, [](const File& a, const File& b) {
                    if (a.root != b.root) return a.root < b.root;
                    return a.relative < b.relative;
                });
            }
            return result;
        }
        using MappingKey = std::pair<std::string, std::string>;
        using Mapping = std::map<MappingKey, File>;
        Mapping readMappings(const std::optional<fs::path>& path, const std::vector<fs::path>& roots,
            const std::set<MappingKey>& allowed)
        {
            Mapping result;
            if (!path) return result;
            if (fs::file_size(*path) > 16U * 1024U * 1024U)
                throw std::runtime_error("resource mapping exceeds 16 MiB");
            std::ifstream in(*path);
            if (!in) throw std::runtime_error("cannot open resource mapping");
            std::string line;
            std::size_t lineNumber{};
            while (std::getline(in, line))
            {
                ++lineNumber;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty() || line[0] == '#') continue;
                if (lineNumber == 1 && line == "bank\tsymbol\troot\tpath") continue;
                const auto a = line.find('\t'), b = line.find('\t', a == line.npos ? a : a+1);
                const auto c = line.find('\t', b == line.npos ? b : b+1);
                if (a == line.npos || b == line.npos || c == line.npos || line.find('\t', c+1) != line.npos)
                    throw std::runtime_error("mapping must have four TSV columns at line " + std::to_string(lineNumber));
                const MappingKey key{lower(line.substr(0,a)), line.substr(a+1,b-a-1)};
                if (!allowed.contains(key)) throw std::runtime_error("unknown bank/symbol in mapping: " + line.substr(0,b));
                const auto rootNumber = std::string_view(line).substr(b+1,c-b-1);
                std::size_t rootIndex{};
                const auto number = std::from_chars(rootNumber.data(),rootNumber.data()+rootNumber.size(),rootIndex);
                if (number.ec != std::errc{} || number.ptr != rootNumber.data()+rootNumber.size() || rootIndex >= roots.size())
                    throw std::runtime_error("invalid root index in mapping line " + std::to_string(lineNumber));
                const auto relative = utf8Path(line.substr(c+1));
                if (relative.empty() || relative.is_absolute() || relative.has_root_name())
                    throw std::runtime_error("mapping path must be relative to its resource root");
                const auto absolute = resolvePath(roots[rootIndex]/relative);
                if (!inside(roots[rootIndex], absolute) || !fs::is_regular_file(absolute))
                    throw std::runtime_error("mapped file missing or outside resource root: " + pathText(relative));
                if (!result.emplace(key, File{rootIndex, relative, absolute}).second)
                    throw std::runtime_error("duplicate bank/symbol mapping");
            }
            if (in.bad()) throw std::runtime_error("mapping read failed");
            return result;
        }
        std::vector<std::string> namesFor(const LegacyManifestEntry& entry)
        {
            const auto prefixEnd = entry.symbol.find('_');
            const auto stem = entry.symbol.substr(prefixEnd+1);
            std::vector<std::string> extensions;
            switch (entry.type)
            {
            case LegacyDataType::Bitmap: extensions = {"bmp"}; break;
            case LegacyDataType::Uap: extensions = {"tab", "uap"}; break;
            case LegacyDataType::Chunky: extensions = {"cnk"}; break;
            case LegacyDataType::Hmd: extensions = {"hmd"}; break;
            case LegacyDataType::Wave: extensions = {"wav"}; break;
            default: throw std::runtime_error("unsupported manifest type");
            }
            std::vector<std::string> names;
            for (const auto& ext : extensions)
            {
                names.push_back(lower(stem+"."+ext));
                names.push_back(lower(entry.symbol+"."+ext));
            }
            return names;
        }
        fs::path writeArchive(const fs::path& run, const BankResult& bank,
            std::uint16_t group, const std::vector<ArchiveBuildItem>& items)
        {
            if (bank.available == 0) return {}; // Never emit an all-empty bank.
            const auto folder = run/(bank.complete() ? "Dat_Mon" : "partial");
            fs::create_directories(folder);
            const auto target = folder/(bank.name+(bank.complete() ? ".dat" : ".partial.dat"));
            auto pending = target;
            pending += ".writing";
            const auto written = writeLegacyDataArchive(pending, items);
            if (!written) throw std::runtime_error(written.error().detail);
            ArchiveOpenOptions options;
            options.checksumPolicy = ChecksumPolicy::Verify;
            auto opened = LegacyDataArchive::open(pending, group, options);
            if (!opened) throw std::runtime_error(opened.error().detail);
            if ((*opened)->itemCount() != items.size())
                throw std::runtime_error("archive read-back changed the number of tags");
            for (std::size_t tag = 0; tag < items.size(); ++tag)
            {
                const auto meta = (*opened)->metadata(static_cast<DataTag>(tag));
                if (!meta || meta->type != items[tag].type || meta->present() == items[tag].emptySlot())
                    throw std::runtime_error("archive read-back changed a tag or type");
                if (items[tag].emptySlot()) continue;
                const auto payload = (*opened)->load(static_cast<DataTag>(tag));
                if (!payload || **payload != items[tag].payload)
                    throw std::runtime_error("archive read-back changed resource bytes");
            }
            if (group == 9)
            {
                const auto language = LanguageCatalog::open(LanguageId::EnglishUs, *opened);
                if (!language) throw std::runtime_error(language.error().detail);
            }
            (*opened)->close();
            fs::rename(pending, target); // Published only after disk read-back.
            return target;
        }
        std::string joined(const std::vector<std::string>& values)
        {
            std::string result;
            for (const auto& value : values) { if (!result.empty()) result += " | "; result += value; }
            return result;
        }
    }

    bool Result::complete() const noexcept
    { return banks.size() == 8 && std::ranges::all_of(banks, &BankResult::complete); }

    Result run(const Request& request)
    {
        const auto source = resolvePath(request.sourceRoot);
        const auto output = resolvePath(request.outputRoot);
        std::vector<fs::path> roots{source};
        for (const auto& extra : request.extraRoots) roots.push_back(resolvePath(extra));
        for (const auto& input : roots) rejectOverlap(output, input);
#ifdef MONOPOLY_PROTECTED_SOURCE_ROOT
        rejectOverlap(output, resolvePath(utf8Path(MONOPOLY_PROTECTED_SOURCE_ROOT)));
#endif
        const auto files = indexFiles(roots);
        std::map<std::string, LegacyBankManifest> manifests;
        std::set<MappingKey> allowed;
        for (const auto& bank : Banks)
        {
            auto manifest = readDmakeManifest(source/"Dat_Mon"/bank.header);
            if (!manifest) throw std::runtime_error(pathText(manifest.error().path) + ": " + manifest.error().detail);
            for (const auto& entry : manifest->entries) allowed.emplace(bank.name,entry.symbol);
            manifests.emplace(bank.name,std::move(*manifest));
        }
        const auto mappings = readMappings(request.mappings, roots, allowed);
        const auto english = readEnglishText(source/"Dat_Mon/dat_lang.h", source/"English.atr");
        fs::create_directories(output);
        Result result;
        for (unsigned attempt = 0; attempt < 100; ++attempt)
        {
            const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
            const auto path = output/("run-"+std::to_string(stamp)+"-"+std::to_string(attempt));
            if (fs::create_directory(path)) { result.directory=path; break; }
        }
        if (result.directory.empty()) throw std::runtime_error("cannot create an exclusive assembly directory");
        auto details = report(result.directory/"resources.tsv");
        details << "bank\ttag_or_message_id\ttype\tsymbol\tstatus\troot\tpath\tdetail\n";
        auto bankReport = report(result.directory/"banks.tsv");
        bankReport << "bank\texpected\tavailable\tinvalid\tstatus\tarchive\n";
        std::set<fs::path> consumed;
        const auto describeBank = [&](const BankResult& bank) {
            const auto status = bank.complete() ? "MANIFEST_COMPLETE" : "INCOMPLETE";
            bankReport << bank.name << '\t' << bank.expected << '\t' << bank.available << '\t'
                << bank.invalid << '\t' << status << '\t' << cell(pathText(bank.archive)) << '\n';
            std::cout << bank.name << ": " << bank.available << '/' << bank.expected << " " << status
                << "; invalid=" << bank.invalid << '\n';
        };
        for (const auto& definition : Banks)
        {
            const auto& manifest = manifests.at(definition.name);
            BankResult bank{definition.name, manifest.entries.size()};
            std::vector<ArchiveBuildItem> items(manifest.declaredItemCount);
            std::uintmax_t totalPayload{};
            for (const auto& entry : manifest.entries)
            {
                std::vector<File> candidates;
                const auto mapping = mappings.find({bank.name,entry.symbol});
                const bool explicitMapping = mapping != mappings.end();
                if (explicitMapping) candidates.push_back(mapping->second);
                else
                {
                    std::set<fs::path> seen;
                    for (const auto& name : namesFor(entry))
                        if (const auto found = files.find(name); found != files.end())
                            for (const auto& file : found->second)
                                if (seen.insert(file.absolute).second) candidates.push_back(file);
                }
                std::string status, why;
                std::string fromRoot, fromPath;
                if (candidates.empty()) { status="missing"; why=joined(namesFor(entry)); }
                else if (candidates.size() != 1)
                {
                    status="ambiguous";
                    for (const auto& file : candidates) why += std::to_string(file.root)+":"+pathText(file.relative)+" | ";
                }
                else
                {
                    const auto& file = candidates.front();
                    fromRoot=std::to_string(file.root); fromPath=pathText(file.relative);
                    try
                    {
                        const auto size = fs::file_size(file.absolute);
                        if (size > PayloadLimit || totalPayload > PayloadLimit - size)
                            throw std::runtime_error("bank payload budget exceeds 512 MiB");
                        auto bytes = readPayload(file.absolute);
                        validate(entry.type,bytes);
                        totalPayload += bytes.size();
                        items[entry.tag]={entry.type,std::move(bytes)};
                        ++bank.available;
                        consumed.insert(file.absolute);
                        status="packed";
                        why=explicitMapping ? "explicit mapping; format inspected" : "unique exact filename; format inspected";
                    }
                    catch (const std::exception& error) { status="invalid"; why=error.what(); ++bank.invalid; }
                }
                details << bank.name << '\t' << entry.tag << '\t' << legacyDataTypeName(entry.type) << '\t'
                    << entry.symbol << '\t' << status << '\t' << fromRoot << '\t' << cell(fromPath) << '\t' << cell(why) << '\n';
            }
            bank.archive=writeArchive(result.directory,bank,definition.group,items);
            describeBank(bank);
            result.banks.push_back(std::move(bank));
        }
        BankResult language{"dat_ln01",english.requiredNames.size(),english.text.size()};
        for (const auto& [id,name] : english.requiredNames)
        {
            const bool present=english.text.contains(id);
            details << language.name << '\t' << id << "\tString\t" << name << '\t'
                << (present ? "packed" : "missing") << "\t0\tEnglish.atr\t"
                << (present ? "exact symbolic ID; ASCII to UTF-16LE" : "no exact symbolic text in supplied ATR") << '\n';
        }
        for (const auto& name : english.unmappedSymbols)
            details << "dat_ln01\t\tString\t" << name
                << "\tunmapped-symbol\t0\tEnglish.atr\tnot defined in active dat_lang.h; no numeric ID guessed\n";
        language.archive=writeArchive(result.directory,language,9,buildEnglishItems(english));
        describeBank(language);
        result.banks.push_back(std::move(language));
        auto unused = report(result.directory/"unassigned-files.tsv");
        unused << "root\tpath\treason\n";
        const std::set<std::string> extensions{".bmp",".tab",".uap",".wav",".cnk",".hmd"};
        for (const auto& [name, matches] : files)
        {
            (void)name;
            for (const auto& file : matches)
                if (extensions.contains(lower(pathText(file.relative.extension()))) && !consumed.contains(file.absolute))
                    unused << file.root << '\t' << cell(pathText(file.relative))
                        << "\tno unique manifest assignment; may already be used as an external texture\n";
        }
        auto summary = report(result.directory/"README.txt");
        summary << "Monopoly archive assembly\n"
            << (result.complete() ? "All manifest entries have payloads.\n" : "INCOMPLETE - not a playable game data set.\n")
            << "No files were modified in Source/ or installed in the game's Dat_Mon.\n"
            << "Partial archives contain ONLY recovered payloads. Original numeric tags are preserved.\n"
            << "A missing slot is not a substitute for an animation, sound, model or image.\n"
            << "Dat_Mon/ contains only banks complete against their manifests; partial/ is never installed.\n"
            << "Language conversion only maps exact symbols shared by English.atr and dat_lang.h.\n"
            << "No numbers from English.A or unrelated legacy headers were assumed equivalent.\n"
            << "resources.tsv lists every resource decision. banks.tsv summarizes banks.\n"
            << "unassigned-files.tsv also lists external texture files; unassigned does NOT mean missing.\n"
            << "Container CRC and byte-for-byte read-back are checked during publication.\n"
            << "Payload structural inspection does not establish cross-bank references or gameplay fidelity.\n";
        for (std::size_t n=0;n<roots.size();++n) summary << "resource root " << n << ": " << pathText(roots[n]) << '\n';
        summary.close(); unused.close(); details.close(); bankReport.close();
        std::cout << "Assembly directory: " << pathText(result.directory) << '\n'
            << (result.complete() ? "ASSEMBLED; gameplay not qualified\n" : "INCOMPLETE; runtime installation left untouched\n");
        return result;
    }
}
