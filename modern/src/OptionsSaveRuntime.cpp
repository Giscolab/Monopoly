#include "OptionsSaveRuntime.hpp"

#include "FontRuntime.hpp"
#include "LanguageResources.hpp"
#include "ResourceRuntime.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <bit>
#include <fstream>
#include <limits>
#include <system_error>

namespace monopoly::optionsui
{
    namespace
    {
        inline constexpr std::size_t DescriptionUnits = 100;
        inline constexpr std::size_t CustomBoardBytes = 100;
        inline constexpr std::size_t MetadataBytes =
            DescriptionUnits * 2U + 8U + rules::SquareCount * 4U + CustomBoardBytes;
        inline constexpr int SlotLeft = 152;
        inline constexpr int SlotTop = 112;
        inline constexpr int SlotWidth = 496;
        inline constexpr int SlotHeight = 34;
        inline constexpr int SlotStepY = 68;
        inline constexpr int MaximumDescriptionWidth = 446;
        inline constexpr std::uint32_t ButtonCancelTextId = 932;
        inline constexpr std::uint32_t ButtonOkayTextId = 933;
        void writeU16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
        {
            bytes.push_back(static_cast<std::uint8_t>(value));
            bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
        }

        void writeU32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
        {
            for (unsigned shift = 0; shift < 32; shift += 8)
                bytes.push_back(static_cast<std::uint8_t>(value >> shift));
        }

        [[nodiscard]] std::uint16_t readU16(
            std::span<const std::uint8_t> bytes, std::size_t& offset) noexcept
        {
            const std::uint16_t result = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bytes[offset]) |
                (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U));
            offset += 2;
            return result;
        }

        [[nodiscard]] std::uint32_t readU32(
            std::span<const std::uint8_t> bytes, std::size_t& offset) noexcept
        {
            std::uint32_t result{};
            for (unsigned shift = 0; shift < 32; shift += 8)
                result |= static_cast<std::uint32_t>(bytes[offset++]) << shift;
            return result;
        }

        [[nodiscard]] std::vector<std::uint8_t> encodeMetadata(
            const SaveMetadata& metadata)
        {
            std::vector<std::uint8_t> bytes;
            bytes.reserve(MetadataBytes);
            for (std::size_t index = 0; index < DescriptionUnits; ++index)
            {
                const std::uint16_t value = index < metadata.description.size()
                    ? static_cast<std::uint16_t>(metadata.description[index]) : 0U;
                writeU16(bytes, value);
            }
            writeU32(bytes, std::bit_cast<std::uint32_t>(metadata.city));
            writeU32(bytes, std::bit_cast<std::uint32_t>(metadata.system));
            for (const auto earning : metadata.squareGameEarnings)
                writeU32(bytes, earning);

            for (std::size_t index = 0; index < CustomBoardBytes; ++index)
            {
                const std::uint8_t value = index < metadata.customBoardName.size()
                    ? static_cast<std::uint8_t>(metadata.customBoardName[index])
                    : std::uint8_t{0};
                bytes.push_back(value);
            }
            return bytes;
        }

        [[nodiscard]] std::expected<SaveMetadata, std::string> decodeMetadata(
            std::span<const std::uint8_t> bytes)
        {
            if (bytes.size() < MetadataBytes)
                return std::unexpected("saved-game metadata is truncated");

            SaveMetadata metadata{};
            std::size_t offset{};
            bool descriptionTerminated = false;
            for (std::size_t index = 0; index < DescriptionUnits; ++index)
            {
                const auto value = readU16(bytes, offset);
                if (value == 0)
                {
                    descriptionTerminated = true;
                    continue;
                }
                if (descriptionTerminated ||
                    metadata.description.size() + 1U >= DescriptionUnits)
                    continue;
                metadata.description.push_back(static_cast<char16_t>(value));
            }
            metadata.city = std::bit_cast<std::int32_t>(readU32(bytes, offset));
            metadata.system = std::bit_cast<std::int32_t>(readU32(bytes, offset));
            for (auto& earning : metadata.squareGameEarnings)
                earning = readU32(bytes, offset);

            for (std::size_t index = 0; index < CustomBoardBytes; ++index)
            {
                const auto value = static_cast<char>(bytes[offset++]);
                if (value == '\0') break;
                metadata.customBoardName.push_back(value);
            }
            return metadata;
        }

        [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> readFile(
            const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) return std::unexpected("unable to open " + path.string());
            const auto end = input.tellg();
            if (end < 0) return std::unexpected("unable to size " + path.string());
            const auto size = static_cast<std::uint64_t>(end);
            if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
                return std::unexpected("file is too large: " + path.string());
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
            input.seekg(0, std::ios::beg);
            if (!bytes.empty() && !input.read(
                    reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size())))
                return std::unexpected("unable to read " + path.string());
            return bytes;
        }

        [[nodiscard]] std::expected<void, std::string> writeFile(
            const std::filesystem::path& path, std::span<const std::uint8_t> bytes)
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output) return std::unexpected("unable to create " + path.string());
            if (!bytes.empty())
            {
                output.write(reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
                if (!output) return std::unexpected("unable to write " + path.string());
            }
            return {};
        }

        void appendUtf8(std::u16string& output, std::string_view text)
        {
            for (std::size_t index = 0; index < text.size();)
            {
                const auto first = static_cast<std::uint8_t>(text[index]);
                std::uint32_t codepoint{};
                std::size_t count{};
                if (first < 0x80U) { codepoint = first; count = 1; }
                else if ((first & 0xE0U) == 0xC0U) { codepoint = first & 0x1FU; count = 2; }
                else if ((first & 0xF0U) == 0xE0U) { codepoint = first & 0x0FU; count = 3; }
                else if ((first & 0xF8U) == 0xF0U) { codepoint = first & 0x07U; count = 4; }
                else { ++index; continue; }
                if (index + count > text.size()) break;
                bool valid = true;
                for (std::size_t part = 1; part < count; ++part)
                {
                    const auto byte = static_cast<std::uint8_t>(text[index + part]);
                    if ((byte & 0xC0U) != 0x80U) { valid = false; break; }
                    codepoint = (codepoint << 6U) | (byte & 0x3FU);
                }
                if (!valid) { ++index; continue; }
                index += count;
                if (codepoint >= 0xD800U && codepoint <= 0xDFFFU) continue;
                if (codepoint <= 0xFFFFU)
                {
                    output.push_back(static_cast<char16_t>(codepoint));
                }
                else if (codepoint <= 0x10FFFFU)
                {
                    codepoint -= 0x10000U;
                    output.push_back(static_cast<char16_t>(0xD800U + (codepoint >> 10U)));
                    output.push_back(static_cast<char16_t>(0xDC00U + (codepoint & 0x3FFU)));
                }
            }
        }

        [[nodiscard]] std::string toUtf8(std::u16string_view text)
        {
            std::string output;
            output.reserve(text.size());
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                std::uint32_t cp = static_cast<std::uint16_t>(text[index]);
                if (cp >= 0xD800U && cp <= 0xDBFFU && index + 1 < text.size())
                {
                    const auto low = static_cast<std::uint16_t>(text[index + 1]);
                    if (low >= 0xDC00U && low <= 0xDFFFU)
                    {
                        cp = 0x10000U + ((cp - 0xD800U) << 10U) + (low - 0xDC00U);
                        ++index;
                    }
                }
                if (cp <= 0x7FU)
                    output.push_back(static_cast<char>(cp));
                else if (cp <= 0x7FFU)
                {
                    output.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
                    output.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
                }
                else if (cp <= 0xFFFFU)
                {
                    output.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
                    output.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
                    output.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
                }
                else
                {
                    output.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
                    output.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
                    output.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
                    output.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
                }
            }
            return output;
        }

        [[nodiscard]] std::optional<std::pair<int, int>> textMetrics(
            std::uint32_t messageId, fonts::Runtime* fontRuntime,
            const std::shared_ptr<const data::ResourceSnapshot>& resources)
        {
            if (!fontRuntime || !fontRuntime->ready() || !resources)
                return std::nullopt;
            const auto language = resources->language();
            if (!language || !language->catalog) return std::nullopt;
            const auto text = language->catalog->message(messageId);
            if (!text) return std::nullopt;
            const auto measured = fontRuntime->measure(toUtf8(**text));
            if (!measured) return std::nullopt;
            return std::pair<int, int>{measured->width, measured->height};
        }

        [[nodiscard]] bool descriptionHasRoom(
            const std::u16string& text, fonts::Runtime* fontRuntime)
        {
            if (text.size() + 1U >= DescriptionUnits) return false;
            if (!fontRuntime || !fontRuntime->ready()) return true;
            const auto measured = fontRuntime->measure(toUtf8(text));
            return !measured || measured->width < MaximumDescriptionWidth;
        }

        [[nodiscard]] std::uint32_t legacyEarning(std::int64_t value) noexcept
        {
            if (value <= 0) return 0;
            if (static_cast<std::uint64_t>(value) >
                std::numeric_limits<std::uint32_t>::max())
                return std::numeric_limits<std::uint32_t>::max();
            return static_cast<std::uint32_t>(value);
        }
    }

    std::filesystem::path saveGameDirectory()
    {
        const char* base = SDL_GetBasePath();
        if (base == nullptr || *base == '\0') return {};
        return std::filesystem::path(base) / "savegame";
    }

    std::filesystem::path gameBlobPath(std::size_t slot)
    {
        const auto root = saveGameDirectory();
        if (root.empty() || slot >= SaveSlotCount) return {};
        return root / ("game" + std::to_string(slot + 1U) + ".msv");
    }

    std::filesystem::path gameMetadataPath(std::size_t slot)
    {
        const auto root = saveGameDirectory();
        if (root.empty() || slot >= SaveSlotCount) return {};
        return root / ("game" + std::to_string(slot + 1U) + ".sgd");
    }

    Rect saveSlotRect(std::size_t slot) noexcept
    {
        if (slot >= SaveSlotCount) return {};
        const int top = SlotTop + static_cast<int>(slot) * SlotStepY;
        return {SlotLeft, top, SlotLeft + SlotWidth, top + SlotHeight};
    }

    Rect dialogOkayRect(int width, int height) noexcept
    {
        return {500, 430, 500 + std::max(width, 0), 430 + std::max(height, 0)};
    }

    Rect dialogCancelRect(int width, int height) noexcept
    {
        return {600, 430, 600 + std::max(width, 0), 430 + std::max(height, 0)};
    }

    std::expected<void, std::string> refreshSaveSlots(
        SaveRuntimeState& state, FileDialogMode mode)
    {
        if (mode == FileDialogMode::None)
            return std::unexpected("cannot open an empty File dialog");
        if (saveGameDirectory().empty())
            return std::unexpected("SDL base path is unavailable for savegame storage");

        state.dialog = mode;
        state.slots = {};
        state.selectedSlot = -1;
        state.draftDescription.clear();
        bool anyMetadata = false;

        for (std::size_t slot = 0; slot < SaveSlotCount; ++slot)
        {
            const auto path = gameMetadataPath(slot);
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error) || error) continue;
            anyMetadata = true;
            state.slots[slot].occupied = true;
            const auto bytes = readFile(path);
            if (!bytes) continue;
            const auto decoded = decodeMetadata(*bytes);
            if (decoded) state.slots[slot].metadata = *decoded;
        }

        // UDOPTIONS_DisplayLoad/SaveDialogBox sets CurrentGameFile to zero
        // whenever at least one metadata file was discovered.
        state.selectedSlot = anyMetadata ? 0 : -1;
        ++state.revision;
        return {};
    }

    void closeSaveDialog(SaveRuntimeState& state) noexcept
    {
        if (state.dialog == FileDialogMode::None) return;
        state.dialog = FileDialogMode::None;
        state.selectedSlot = -1;
        state.draftDescription.clear();
        ++state.revision;
    }

    SaveDialogInput processSaveDialogInput(
        SaveRuntimeState& state,
        const uimsg::Message& message,
        fonts::Runtime* fontRuntime,
        std::shared_ptr<const data::ResourceSnapshot> resources)
    {
        SaveDialogInput result{};
        if (state.dialog == FileDialogMode::None) return result;
        result.consumed = true;

        const auto requestOkay = [&]()
        {
            if (state.selectedSlot < 0 ||
                state.selectedSlot >= static_cast<int>(SaveSlotCount))
                return;
            if (state.dialog == FileDialogMode::Load)
                result.requestLoad = true;
            else
                result.requestSave = true;
            result.closeDialog = true;
            result.playClick = true;
        };

        if (message.type == uimsg::Type::KeyboardPressed)
        {
            const auto key = static_cast<SDL_Scancode>(message.numberA);
            if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER)
            {
                requestOkay();
                return result;
            }
            if (key == SDL_SCANCODE_BACKSPACE &&
                state.dialog == FileDialogMode::Save &&
                state.selectedSlot >= 0 && !state.draftDescription.empty())
            {
                if (state.draftDescription.size() >= 2U)
                {
                    const auto last = static_cast<std::uint16_t>(state.draftDescription.back());
                    const auto previous = static_cast<std::uint16_t>(
                        state.draftDescription[state.draftDescription.size() - 2U]);
                    if (last >= 0xDC00U && last <= 0xDFFFU &&
                        previous >= 0xD800U && previous <= 0xDBFFU)
                        state.draftDescription.pop_back();
                }
                state.draftDescription.pop_back();
                ++state.revision;
                return result;
            }
            return result;
        }

        if (message.type == uimsg::Type::TextInput &&
            state.dialog == FileDialogMode::Save &&
            state.selectedSlot >= 0)
        {
            std::u16string added;
            appendUtf8(added, message.text);
            bool changed = false;
            for (std::size_t index = 0; index < added.size();)
            {
                std::size_t units = 1;
                const auto first = static_cast<std::uint16_t>(added[index]);
                if (first >= 0xD800U && first <= 0xDBFFU &&
                    index + 1U < added.size())
                    units = 2;
                if (state.draftDescription.size() + units >= DescriptionUnits ||
                    !descriptionHasRoom(state.draftDescription, fontRuntime))
                    break;
                state.draftDescription.append(added, index, units);
                index += units;
                changed = true;
            }
            if (changed) ++state.revision;
            return result;
        }

        if (message.type != uimsg::Type::MouseLeftDown) return result;
        const int x = static_cast<int>(message.numberA);
        const int y = static_cast<int>(message.numberB);
        for (std::size_t slot = 0; slot < SaveSlotCount; ++slot)
        {
            if (!saveSlotRect(slot).contains(x, y)) continue;
            if (state.dialog == FileDialogMode::Load && !state.slots[slot].occupied)
                return result;
            state.selectedSlot = static_cast<int>(slot);
            state.draftDescription.clear();
            ++state.revision;
            return result;
        }

        const auto okay = textMetrics(ButtonOkayTextId, fontRuntime, resources);
        const auto cancel = textMetrics(ButtonCancelTextId, fontRuntime, resources);
        if (okay && dialogOkayRect(okay->first, okay->second).contains(x, y))
        {
            requestOkay();
            return result;
        }
        if (cancel && dialogCancelRect(cancel->first, cancel->second).contains(x, y))
        {
            result.closeDialog = true;
            result.playClick = true;
            return result;
        }
        return result;
    }

    std::expected<std::vector<std::uint8_t>, std::string>
    readSelectedGameBlob(const SaveRuntimeState& state)
    {
        if (state.dialog != FileDialogMode::Load ||
            state.selectedSlot < 0 ||
            state.selectedSlot >= static_cast<int>(SaveSlotCount))
            return std::unexpected("no load-game slot is selected");
        const auto slot = static_cast<std::size_t>(state.selectedSlot);
        if (!state.slots[slot].occupied)
            return std::unexpected("selected load-game slot is empty");
        const auto bytes = readFile(gameBlobPath(slot));
        if (!bytes) return std::unexpected(bytes.error());
        if (bytes->size() < 12U)
            return std::unexpected("saved game blob is shorter than the retail minimum");
        return *bytes;
    }

    std::expected<void, std::string> beginPendingSave(
        SaveRuntimeState& state,
        const rules::GameState& ruleState,
        int city,
        int system,
        std::string customBoardName)
    {
        if (state.dialog != FileDialogMode::Save ||
            state.selectedSlot < 0 ||
            state.selectedSlot >= static_cast<int>(SaveSlotCount))
            return std::unexpected("no save-game slot is selected");

        const auto slot = static_cast<std::size_t>(state.selectedSlot);
        SaveMetadata metadata{};
        metadata.description = state.draftDescription;
        metadata.city = city;
        metadata.system = system;
        if (city == -1)
        {
            if (customBoardName.size() >= CustomBoardBytes)
                customBoardName.resize(CustomBoardBytes - 1U);
            metadata.customBoardName = std::move(customBoardName);
        }
        for (std::size_t square = 0; square < rules::SquareCount; ++square)
            metadata.squareGameEarnings[square] =
                legacyEarning(ruleState.squares[square].gameEarnings);

        state.pendingSaveSlot = slot;
        state.pendingMetadata = std::move(metadata);
        return {};
    }

    std::expected<void, std::string> persistPendingSave(
        SaveRuntimeState& state,
        std::span<const std::uint8_t> gameBlob)
    {
        if (!state.pendingSaveSlot || *state.pendingSaveSlot >= SaveSlotCount)
            return std::unexpected("no pending save-game request exists");
        if (gameBlob.empty())
            return std::unexpected("RULE returned an empty save-game blob");

        const auto slot = *state.pendingSaveSlot;
        const auto metadata = state.pendingMetadata;
        state.pendingSaveSlot.reset();
        state.pendingMetadata = {};

        const auto root = saveGameDirectory();
        if (root.empty())
            return std::unexpected("SDL base path is unavailable for savegame storage");
        std::error_code error;
        std::filesystem::create_directories(root, error);
        if (error)
            return std::unexpected("unable to create savegame directory: " + error.message());

        // Retail writes the sidecar first, but a sidecar failure does not prevent
        // the authoritative .msv blob from being attempted.
        const auto encoded = encodeMetadata(metadata);
        const auto metadataWrite = writeFile(gameMetadataPath(slot), encoded);
        const auto blobWrite = writeFile(gameBlobPath(slot), gameBlob);
        if (!blobWrite) return std::unexpected(blobWrite.error());

        state.slots[slot].occupied = true;
        state.slots[slot].metadata = metadata;
        ++state.revision;
        (void)metadataWrite;
        return {};
    }

    std::expected<void, std::string> applySelectedMetadata(
        const SaveRuntimeState& state,
        rules::GameState& ruleState,
        int& city,
        int& system)
    {
        if (state.selectedSlot < 0 ||
            state.selectedSlot >= static_cast<int>(SaveSlotCount))
            return std::unexpected("no saved-game metadata slot is selected");
        const auto slot = static_cast<std::size_t>(state.selectedSlot);
        if (!state.slots[slot].occupied)
            return std::unexpected("selected saved-game metadata slot is empty");

        const auto& metadata = state.slots[slot].metadata;
        city = metadata.city;
        system = metadata.system;
        for (std::size_t square = 0; square < rules::SquareCount; ++square)
            ruleState.squares[square].gameEarnings =
                static_cast<std::int64_t>(metadata.squareGameEarnings[square]);
        return {};
    }
}
