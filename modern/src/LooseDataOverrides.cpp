#include "LooseDataOverrides.hpp"

#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace monopoly::data
{
    namespace
    {
        using namespace std::string_view_literals;

        constexpr std::uint64_t MaximumPayloadBytes =
            512ULL * 1024ULL * 1024ULL;

        [[nodiscard]] DataError manifestError(
            DataErrorCode code,
            const std::filesystem::path& path,
            std::size_t line,
            std::string detail,
            std::optional<DataTag> tag = std::nullopt)
        {
            if (line != 0)
                detail = "line " + std::to_string(line) + ": " + detail;
            return {code, path, tag, std::move(detail)};
        }

        [[nodiscard]] std::optional<std::array<std::string_view, 4>>
        splitFields(std::string_view line)
        {
            std::array<std::string_view, 4> result{};
            for (std::size_t index = 0; index < result.size(); ++index)
            {
                const auto separator = line.find('\t');
                if (index + 1 == result.size())
                {
                    if (separator != std::string_view::npos) return std::nullopt;
                    result[index] = line;
                    return result;
                }
                if (separator == std::string_view::npos) return std::nullopt;
                result[index] = line.substr(0, separator);
                line.remove_prefix(separator + 1);
            }
            return result;
        }

        [[nodiscard]] std::optional<std::uint16_t>
        parseU16(std::string_view value)
        {
            unsigned parsed{};
            int base = 10;
            if (value.starts_with("0x") || value.starts_with("0X"))
            {
                value.remove_prefix(2);
                base = 16;
            }
            if (value.empty()) return std::nullopt;
            const auto converted = std::from_chars(
                value.data(), value.data() + value.size(), parsed, base);
            if (converted.ec != std::errc{} ||
                converted.ptr != value.data() + value.size() ||
                parsed > std::numeric_limits<std::uint16_t>::max())
                return std::nullopt;
            return static_cast<std::uint16_t>(parsed);
        }

        [[nodiscard]] std::optional<LegacyDataType>
        parseType(std::string_view name)
        {
            constexpr std::array types{
                std::pair{"Bitmap"sv, LegacyDataType::Bitmap},
                std::pair{"DopeTable"sv, LegacyDataType::DopeTable},
                std::pair{"Uap"sv, LegacyDataType::Uap},
                std::pair{"Native"sv, LegacyDataType::Native},
                std::pair{"GbmTexture"sv, LegacyDataType::GbmTexture},
                std::pair{"GbmPicture"sv, LegacyDataType::GbmPicture},
                std::pair{"GenericBitmap"sv, LegacyDataType::GenericBitmap},
                std::pair{"Wave"sv, LegacyDataType::Wave},
                std::pair{"String"sv, LegacyDataType::String},
                std::pair{"UserCreated1"sv, LegacyDataType::UserCreated1},
                std::pair{"IndexTable"sv, LegacyDataType::IndexTable},
                std::pair{"Chunky"sv, LegacyDataType::Chunky},
                std::pair{"TextureArray"sv, LegacyDataType::TextureArray},
                std::pair{"Model3D"sv, LegacyDataType::Model3D},
                std::pair{"Pose3D"sv, LegacyDataType::Pose3D},
                std::pair{"Hmd"sv, LegacyDataType::Hmd},
                std::pair{"MeshX"sv, LegacyDataType::MeshX}
            };
            for (const auto& [text, type] : types)
                if (name == text) return type;
            return std::nullopt;
        }

        [[nodiscard]] bool safeRelativePath(
            const std::filesystem::path& path)
        {
            if (path.empty() || path.is_absolute() || path.has_root_path())
                return false;
            for (const auto& component : path)
                if (component == "..") return false;
            return true;
        }

        [[nodiscard]] std::filesystem::path utf8Path(
            std::string_view text)
        {
            std::u8string value;
            value.reserve(text.size());
            for (const unsigned char character : text)
                value.push_back(static_cast<char8_t>(character));
            return std::filesystem::path(std::move(value));
        }

        [[nodiscard]] std::expected<SharedDataBytes, DataError>
        readPayload(
            const std::filesystem::path& path,
            std::optional<DataTag> tag)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                return std::unexpected(DataError{
                    DataErrorCode::ResourceNotFound, path, tag,
                    "loose DATA payload cannot be opened"});

            const auto end = input.tellg();
            if (end <= 0 ||
                static_cast<std::uint64_t>(end) > MaximumPayloadBytes)
                return std::unexpected(DataError{
                    DataErrorCode::InvalidItemRange, path, tag,
                    "loose DATA payload must be 1..512 MiB"});

            auto bytes = std::make_shared<DataBytes>(
                static_cast<std::size_t>(end));
            input.seekg(0, std::ios::beg);
            input.read(
                reinterpret_cast<char*>(bytes->data()),
                static_cast<std::streamsize>(bytes->size()));
            if (!input)
                return std::unexpected(DataError{
                    DataErrorCode::ReadFailed, path, tag,
                    "loose DATA payload could not be read completely"});
            return std::const_pointer_cast<const DataBytes>(bytes);
        }
    }


    std::expected<std::vector<DataSourceOverride>, DataError>
    loadLooseDataOverrides(const std::filesystem::path& manifestPath)
    {
        std::ifstream manifest(manifestPath);
        if (!manifest)
            return std::unexpected(manifestError(
                DataErrorCode::ResourceNotFound,
                manifestPath,
                0,
                "loose DATA manifest cannot be opened"));

        std::vector<DataSourceOverride> result;
        std::unordered_set<DataId> seen;
        std::string line;
        std::size_t lineNumber{};
        while (std::getline(manifest, line))
        {
            ++lineNumber;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.starts_with('#')) continue;
            if (line == "group\ttag\ttype\tpath") continue;

            const auto fields = splitFields(line);
            if (!fields)
                return std::unexpected(manifestError(
                    DataErrorCode::InvalidHeader,
                    manifestPath,
                    lineNumber,
                    "expected four tab-separated columns: group, tag, type, path"));

            const auto group = parseU16((*fields)[0]);
            const auto tag = parseU16((*fields)[1]);
            if (!group || *group == 0 || !tag)
                return std::unexpected(manifestError(
                    DataErrorCode::InvalidGroup,
                    manifestPath,
                    lineNumber,
                    "group and tag must be unsigned 16-bit integers; group cannot be zero"));

            const auto type = parseType((*fields)[2]);
            if (!type)
                return std::unexpected(manifestError(
                    DataErrorCode::InvalidItemType,
                    manifestPath,
                    lineNumber,
                    "unknown LegacyDataType name",
                    static_cast<DataTag>(*tag)));

            const auto relative = utf8Path((*fields)[3]);
            if (!safeRelativePath(relative))
                return std::unexpected(manifestError(
                    DataErrorCode::ResourcePathInvalid,
                    manifestPath,
                    lineNumber,
                    "payload path must stay relative to the manifest directory",
                    static_cast<DataTag>(*tag)));

            const auto id = packDataId(
                *group,
                static_cast<DataTag>(*tag));
            if (!seen.emplace(id).second)
                return std::unexpected(manifestError(
                    DataErrorCode::DuplicateDataId,
                    manifestPath,
                    lineNumber,
                    "logical DATA id is declared more than once",
                    static_cast<DataTag>(*tag)));

            auto payload = readPayload(
                manifestPath.parent_path() / relative,
                static_cast<DataTag>(*tag));
            if (!payload)
                return std::unexpected(payload.error());

            result.push_back(DataSourceOverride{
                id,
                *type,
                std::move(*payload)
            });
        }

        if (!manifest.eof())
            return std::unexpected(manifestError(
                DataErrorCode::ReadFailed,
                manifestPath,
                lineNumber,
                "loose DATA manifest read failed"));

        return result;
    }
}
