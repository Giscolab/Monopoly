#include "OptionsCustomBoardRuntime.hpp"
#include "BoardTextureRuntime.hpp"
#include "LegacyBitmap.hpp"
#include <algorithm>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace monopoly::optionsui
{
    namespace
    {
        bool boardExtension(const std::filesystem::path& path)
        {
            auto extension = path.extension().u8string();
            for (auto& c : extension) if (c >= u8'A' && c <= u8'Z') c += u8'a' - u8'A';
            return extension == u8".brd";
        }
        bool within(const std::filesystem::path& root, const std::filesystem::path& child)
        {
            const auto relative = child.lexically_relative(root);
            if (relative.empty() || relative.is_absolute()) return false;
            for (const auto& part : relative) if (part == "..") return false;
            return relative != ".";
        }
        std::string utf8Name(const std::filesystem::path& path)
        {
            const auto value = path.u8string();
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }
    }
    Rect customBoardSlotRect(std::size_t slot) noexcept
    {
        if (slot >= CustomBoardPageSize) return {};
        const int top = 112 + static_cast<int>(slot) * 68;
        return {152, top, 648, top + 34};
    }
    bool customBoardHasPrevious(const CustomBoardState& state) noexcept
    { return state.pageOffset >= CustomBoardPageSize; }
    bool customBoardHasNext(const CustomBoardState& state) noexcept
    { return state.pageOffset < state.entries.size() && state.entries.size() - state.pageOffset > CustomBoardPageSize; }

    std::expected<void, std::string> openCustomBoardDialog(CustomBoardState& state,
        const std::filesystem::path& moduleDirectory, display::Screen2D previousView)
    {
        if (!moduleDirectory.is_absolute()) return std::unexpected("custom boards require an absolute executable directory");
        std::error_code error;
        const auto module = std::filesystem::canonical(moduleDirectory, error);
        if (error || !std::filesystem::is_directory(module, error))
            return std::unexpected("custom-board executable directory is unavailable");
        CustomBoardState next;
        next.previousView = previousView;
        next.directory = std::filesystem::weakly_canonical(module / "custbrds", error);
        if (error || !within(module, next.directory))
            return std::unexpected("custom-board directory escapes the executable directory");
        const bool exists = std::filesystem::exists(next.directory, error);
        if (error) return std::unexpected("cannot inspect custom-board directory: " + error.message());
        if (exists)
        {
            std::filesystem::directory_iterator iterator(next.directory, error), end;
            if (error) return std::unexpected("cannot enumerate custom boards: " + error.message());
            for (; iterator != end && next.entries.size() < CustomBoardLimit; iterator.increment(error))
            {
                if (error) return std::unexpected("cannot enumerate custom boards: " + error.message());
                const auto& entry = *iterator;
                if (!boardExtension(entry.path())) continue;
                const bool regular = entry.is_regular_file(error);
                if (error) return std::unexpected("cannot inspect custom-board file: " + error.message());
                if (!regular) continue;
                const auto canonical = std::filesystem::canonical(entry.path(), error);
                if (error || !within(next.directory, canonical))
                    return std::unexpected("custom-board file escapes its directory");
                const auto name = entry.path().filename();
                next.entries.push_back({name, utf8Name(name)});
            }
            if (error) return std::unexpected("cannot enumerate custom boards: " + error.message());
            std::sort(next.entries.begin(), next.entries.end(), [](const auto& a, const auto& b)
                { return a.fileName < b.fileName; });
        }
        next.active = true;
        next.selectedIndex = next.entries.empty() ? -1 : 0;
        next.revision = state.revision + 1;
        state = std::move(next);
        return {};
    }
    void closeCustomBoardDialog(CustomBoardState& state) noexcept
    {
        state.active = false;
        state.buttonRects = {};
        ++state.revision;
    }
    CustomBoardInput processCustomBoardInput(CustomBoardState& state, const uimsg::Message& message) noexcept
    {
        CustomBoardInput result;
        if (!state.active) return result;
        result.consumed = true;
        if (message.type != uimsg::Type::MouseLeftDown) return result;
        const int x = static_cast<int>(message.numberA), y = static_cast<int>(message.numberB);
        for (std::size_t slot = 0; slot < CustomBoardPageSize && state.pageOffset + slot < state.entries.size(); ++slot)
        {
            if (!customBoardSlotRect(slot).contains(x, y)) continue;
            state.selectedIndex = static_cast<int>(state.pageOffset + slot);
            ++state.revision;
            return result; // Retail slot selection has no click sound.
        }
        for (std::size_t button = 0; button < state.buttonRects.size(); ++button)
        {
            if (!state.buttonRects[button].contains(x, y)) continue;
            if ((button == 2 && !customBoardHasPrevious(state)) ||
                (button == 3 && !customBoardHasNext(state))) return result;
            result.playClick = true;
            if (button == 0)
                result.requestLoad = state.selectedIndex >= 0 && static_cast<std::size_t>(state.selectedIndex) < state.entries.size();
            else if (button == 1) result.closeDialog = true;
            else
            {
                if (button == 2) state.pageOffset -= CustomBoardPageSize;
                else state.pageOffset += CustomBoardPageSize;
                state.selectedIndex = static_cast<int>(state.pageOffset);
                ++state.revision;
            }
            return result;
        }
        return result;
    }
    std::expected<std::uint32_t, std::string> readCustomBoardSecurityVersion()
    {
#ifdef _WIN32
        HKEY key{};
        const auto opened = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"Software\\Hasbro Interactive\\Monopoly\\2.00.101", 0,
            KEY_READ | KEY_WOW64_32KEY, &key);
        if (opened != ERROR_SUCCESS)
            return std::unexpected("custom-board editor registry key is unavailable (Windows error " + std::to_string(opened) + ")");
        DWORD value{}, type{}, bytes = sizeof(value);
        const auto queried = RegQueryValueExW(key, L"Version", nullptr, &type,
            reinterpret_cast<BYTE*>(&value), &bytes);
        RegCloseKey(key);
        if (queried != ERROR_SUCCESS || type != REG_BINARY || bytes != sizeof(value))
            return std::unexpected("custom-board editor Version must be an installed four-byte REG_BINARY value");
        return static_cast<std::uint32_t>(value);
#else
        return std::unexpected("custom-board editor ownership validation requires the Windows registry");
#endif
    }
    std::expected<CustomBoardSelection, std::string> validateCustomBoardFile(
        const std::filesystem::path& directory, const std::filesystem::path& fileName,
        std::uint32_t installedSecurityVersion)
    {
        if (!directory.is_absolute() || fileName.empty() || fileName.has_parent_path() ||
            !boardExtension(fileName) || fileName.stem().empty() || fileName.stem() == "." || fileName.stem() == "..")
            return std::unexpected("invalid custom-board filename or directory");
        std::error_code error;
        const auto root = std::filesystem::canonical(directory, error);
        if (error) return std::unexpected("custom-board directory is unavailable");
        const auto file = std::filesystem::canonical(root / fileName, error);
        if (error || !within(root, file) || !std::filesystem::is_regular_file(file, error))
            return std::unexpected("custom-board file is unavailable or escapes its directory");
        const auto assets = std::filesystem::canonical(root / fileName.stem(), error);
        if (error || !within(root, assets) || !std::filesystem::is_directory(assets, error))
            return std::unexpected("custom-board asset directory is unavailable or escapes its directory");
        std::ifstream input(file, std::ios::binary);
        std::array<unsigned char, 4> bytes{};
        if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
            return std::unexpected("custom-board file lacks its four-byte ownership code");
        const auto version = static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) |
            (static_cast<std::uint32_t>(bytes[3]) << 24U);
        if (version != installedSecurityVersion)
            return std::unexpected("custom board was not created by this installed board editor");
        return CustomBoardSelection{file, assets};
    }
    std::expected<CustomBoardSelection, std::string> validateSelectedCustomBoard(const CustomBoardState& state)
    {
        if (!state.active || state.selectedIndex < 0 || static_cast<std::size_t>(state.selectedIndex) >= state.entries.size())
            return std::unexpected("no custom board is selected");
        const auto version = readCustomBoardSecurityVersion();
        if (!version) return std::unexpected(version.error());
        return validateCustomBoardFile(state.directory, state.entries[static_cast<std::size_t>(state.selectedIndex)].fileName, *version);
    }

    std::expected<std::optional<std::filesystem::path>, std::string>
    restoreSavedCustomBoard(std::string_view savedAssetRoot,
        const data::ResourceSnapshot& resources, int monetarySystem) try
    {
        if (savedAssetRoot.empty()) return std::optional<std::filesystem::path>{};
        if (savedAssetRoot.find('\0') != std::string_view::npos)
            return std::unexpected("saved custom-board path contains NUL");
        const std::filesystem::path savedPath(
            std::u8string(savedAssetRoot.begin(), savedAssetRoot.end()));
        auto paths = savedPath.is_absolute()
            ? data::ResourcePaths::create(std::array{savedPath})
            : data::ResourcePaths::create(resources.paths().roots());
        if (!paths) return std::unexpected(paths.error().detail);
        std::string firstName = "2dboards/2dview01.bmp";
        if (!savedPath.is_absolute())
            firstName = utf8Name(savedPath / firstName);
        const auto first = paths->resolve(firstName);
        if (!first)
        {
            if (first.error().code == data::DataErrorCode::ResourceNotFound)
                return std::optional<std::filesystem::path>{};
            return std::unexpected(first.error().detail);
        }
        const auto root = first->parent_path().parent_path();
        const data::BoardTextureContext context{resources.context().board,
            resources.context().language, -1, monetarySystem, root};
        // UDUTILS_Load2DBoardSet expects all 39 cameras. Decode them before
        // accepting the save, including views other than the current camera.
        for (const auto name : data::twoDimensionalBoardTextureNames())
        {
            const auto path = data::resolveBoardTexturePath(resources.paths(),
                data::BoardMeshKind::CityMedium, data::TextureLocation::CustomBoard2D,
                name, context);
            if (!path) return std::unexpected(path.error());
            std::ifstream input(*path, std::ios::binary | std::ios::ate);
            if (!input) return std::unexpected("cannot open saved custom-board camera");
            const auto size = input.tellg();
            if (size < 0 || static_cast<std::uint64_t>(size) > 4U * 1024U * 1024U)
                return std::unexpected("saved custom-board camera exceeds file budget");
            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            input.seekg(0, std::ios::beg);
            if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            if (!input) return std::unexpected("cannot read saved custom-board camera");
            const auto decoded = data::decodeLegacyBitmapRGBA8(bytes, 800U * 450U);
            if (!decoded) return std::unexpected(decoded.error().detail);
        }
        const auto recipe = context.edition == data::BoardEdition::Usa
            ? data::buildUsaTextureRecipe(data::BoardMeshKind::CityMedium, data::TextureResolution::Pixels128)
            : data::buildEuropeanTextureRecipe(data::BoardMeshKind::CityMedium, data::TextureResolution::Pixels128);
        if (!recipe) return std::unexpected(std::string(recipe.error().detail));
        const auto textures = data::loadBoardTextureImages(resources.paths(), *recipe, context);
        if (!textures) return std::unexpected(textures.error());
        return std::optional<std::filesystem::path>{root};
    }
    catch (const std::filesystem::filesystem_error& error)
    {
        return std::unexpected(std::string("invalid saved custom-board path: ") + error.what());
    }
}
