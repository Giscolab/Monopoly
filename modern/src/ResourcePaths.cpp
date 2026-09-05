#include "ResourcePaths.hpp"

#include <algorithm>
#include <string>
#include <system_error>
#include <utility>

namespace monopoly::data
{
    namespace
    {
        [[nodiscard]] DataError pathError(
            DataErrorCode code,
            const std::filesystem::path& path,
            std::string detail)
        {
            return { code, path, std::nullopt, std::move(detail) };
        }


        [[nodiscard]] char8_t foldedAscii(char8_t value) noexcept
        {
            return value >= u8'A' && value <= u8'Z'
                ? static_cast<char8_t>(value + (u8'a' - u8'A'))
                : value;
        }


        [[nodiscard]] bool sameLegacyName(
            std::u8string_view left,
            std::u8string_view right) noexcept
        {
            return left.size() == right.size() && std::equal(
                left.begin(), left.end(), right.begin(),
                [] (char8_t a, char8_t b)
                {
                    return foldedAscii(a) == foldedAscii(b);
                });
        }


        [[nodiscard]] bool absent(const std::error_code& error) noexcept
        {
            return error == std::errc::no_such_file_or_directory ||
                error == std::errc::not_a_directory;
        }


        [[nodiscard]] std::expected<std::vector<std::u8string>, DataError>
        splitRelativePath(std::string_view input)
        {
            if (input.empty() || input.find('\0') != std::string_view::npos ||
                input.front() == '/' || input.front() == '\\' ||
                input.find(':') != std::string_view::npos)
            {
                return std::unexpected(pathError(
                    DataErrorCode::ResourcePathInvalid, {},
                    "resource path must be a nonempty relative UTF-8 path"));
            }

            std::vector<std::u8string> components;
            std::size_t offset = 0;

            while (offset < input.size())
            {
                const auto separator = input.find_first_of("/\\", offset);
                const auto end = separator == std::string_view::npos
                    ? input.size() : separator;
                const auto component = input.substr(offset, end - offset);

                if (component == ".." ||
                    (component == "." && end == input.size()))
                {
                    return std::unexpected(pathError(
                        DataErrorCode::ResourcePathInvalid, {},
                        "parent traversal or a terminal dot is not a resource file"));
                }

                if (!component.empty() && component != ".")
                {
                    // Byte-preserving UTF-8 construction also selects the
                    // UTF-8 conversion overload of filesystem::path on Windows.
                    std::u8string utf8;
                    utf8.reserve(component.size());
                    for (const auto byte : component)
                    {
                        utf8.push_back(static_cast<char8_t>(byte));
                    }
                    components.push_back(std::move(utf8));
                }

                offset = end + 1;
            }

            if (components.empty() || input.back() == '/' ||
                input.back() == '\\')
            {
                return std::unexpected(pathError(
                    DataErrorCode::ResourcePathInvalid, {},
                    "resource path must name a file"));
            }

            return components;
        }


        [[nodiscard]] std::expected<std::filesystem::path, DataError>
        findComponent(
            const std::filesystem::path& directory,
            std::u8string_view wanted)
        {
            std::error_code error;
            std::filesystem::directory_iterator iterator(directory, error);
            if (error)
            {
                return std::unexpected(pathError(
                    absent(error) ? DataErrorCode::ResourceNotFound
                                  : DataErrorCode::ResourcePathFailed,
                    directory, "cannot enumerate resource directory: " +
                        error.message()));
            }

            std::filesystem::path match;
            const std::filesystem::directory_iterator end;
            while (iterator != end)
            {
                if (sameLegacyName(iterator->path().filename().u8string(), wanted))
                {
                    if (!match.empty())
                    {
                        return std::unexpected(pathError(
                            DataErrorCode::ResourcePathAmbiguous, directory,
                            "multiple directory entries match the legacy ASCII case"));
                    }
                    match = iterator->path();
                }

                iterator.increment(error);
                if (error)
                {
                    return std::unexpected(pathError(
                        DataErrorCode::ResourcePathFailed, directory,
                        "cannot finish enumerating resource directory: " +
                            error.message()));
                }
            }

            if (match.empty())
            {
                return std::unexpected(pathError(
                    DataErrorCode::ResourceNotFound, directory,
                    "resource path component was not found"));
            }

            return match;
        }
    }


    std::expected<ResourcePaths, DataError> ResourcePaths::create(
        std::span<const std::filesystem::path> roots)
    {
        if (roots.empty())
        {
            return std::unexpected(pathError(
                DataErrorCode::ResourcePathInvalid, {},
                "at least one explicit absolute resource root is required"));
        }

        ResourcePaths result;
        for (const auto& root : roots)
        {
            if (root.empty() || !root.is_absolute() ||
                root.native().find(std::filesystem::path::value_type{}) !=
                    std::filesystem::path::string_type::npos)
            {
                return std::unexpected(pathError(
                    DataErrorCode::ResourcePathInvalid, root,
                    "resource roots must be explicit absolute paths without NUL"));
            }

            auto normalized = root.lexically_normal();
            if (normalized != normalized.root_path() &&
                normalized.filename().empty())
            {
                normalized = normalized.parent_path();
            }

            std::error_code error;
            const auto status = std::filesystem::status(normalized, error);
            if (error && !absent(error))
            {
                return std::unexpected(pathError(
                    DataErrorCode::ResourcePathFailed, normalized,
                    "cannot inspect resource root: " + error.message()));
            }
            if (!error && std::filesystem::exists(status) &&
                !std::filesystem::is_directory(status))
            {
                return std::unexpected(pathError(
                    DataErrorCode::ResourcePathInvalid, normalized,
                    "resource root exists but is not a directory"));
            }

            if (std::find(result.roots_.begin(), result.roots_.end(), normalized) ==
                result.roots_.end())
            {
                result.roots_.push_back(std::move(normalized));
            }
        }

        return result;
    }


    std::expected<std::filesystem::path, DataError> ResourcePaths::resolve(
        std::string_view relativePath) const
    {
        const auto components = splitRelativePath(relativePath);
        if (!components)
        {
            return std::unexpected(components.error());
        }

        try
        {
            for (const auto& root : roots_)
            {
                auto candidate = root;
                bool missing = false;
                for (const auto& component : *components)
                {
                    auto match = findComponent(candidate, component);
                    if (!match)
                    {
                        if (match.error().code != DataErrorCode::ResourceNotFound)
                        {
                            return std::unexpected(match.error());
                        }
                        missing = true;
                        break;
                    }
                    candidate = std::move(*match);
                }

                if (missing)
                {
                    continue;
                }

                std::error_code error;
                const auto status = std::filesystem::status(candidate, error);
                if (error && !absent(error))
                {
                    return std::unexpected(pathError(
                        DataErrorCode::ResourcePathFailed, candidate,
                        "cannot inspect resource file: " + error.message()));
                }
                if (!error && std::filesystem::is_regular_file(status))
                {
                    return candidate;
                }
            }
        }
        catch (const std::filesystem::filesystem_error& error)
        {
            return std::unexpected(pathError(
                DataErrorCode::ResourcePathFailed, error.path1(), error.what()));
        }

        return std::unexpected(pathError(
            DataErrorCode::ResourceNotFound, {},
            "resource file was not found in the explicit roots: " +
                std::string(relativePath)));
    }


    std::span<const std::filesystem::path> ResourcePaths::roots() const noexcept
    {
        return roots_;
    }
}
