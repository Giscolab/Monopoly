#include "OptionsSaveRuntime.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <vector>

namespace
{
    int failures = 0;

    void expect(bool condition, const char* message)
    {
        if (condition) return;
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }

    std::optional<std::vector<std::uint8_t>> readExisting(
        const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) return std::nullopt;
        const auto size = input.tellg();
        if (size < 0) return std::nullopt;
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        input.seekg(0, std::ios::beg);
        if (!bytes.empty())
            input.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        return bytes;
    }

    void writeBytes(const std::filesystem::path& path,
        const std::vector<std::uint8_t>& bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!bytes.empty())
            output.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }

    struct Backup final
    {
        std::filesystem::path path;
        std::optional<std::vector<std::uint8_t>> bytes;

        explicit Backup(std::filesystem::path value)
            : path(std::move(value)), bytes(readExisting(path)) {}

        ~Backup()
        {
            std::error_code ignored;
            if (bytes) writeBytes(path, *bytes);
            else std::filesystem::remove(path, ignored);
        }
    };

    void testInput()
    {
        monopoly::optionsui::SaveRuntimeState state{};
        state.dialog = monopoly::optionsui::FileDialogMode::Save;
        state.selectedSlot = 0;

        monopoly::uimsg::Message text{};
        text.type = monopoly::uimsg::Type::TextInput;
        text.text = "Ab";
        const auto typed = monopoly::optionsui::processSaveDialogInput(
            state, text, nullptr, nullptr);
        expect(typed.consumed, "save dialog consumes text input");
        expect(state.draftDescription == u"Ab", "text input updates save description");

        monopoly::uimsg::Message backspace{};
        backspace.type = monopoly::uimsg::Type::KeyboardPressed;
        backspace.numberA = SDL_SCANCODE_BACKSPACE;
        (void)monopoly::optionsui::processSaveDialogInput(
            state, backspace, nullptr, nullptr);
        expect(state.draftDescription == u"A", "backspace edits save description");

        monopoly::uimsg::Message enter{};
        enter.type = monopoly::uimsg::Type::KeyboardPressed;
        enter.numberA = SDL_SCANCODE_RETURN;
        const auto accepted = monopoly::optionsui::processSaveDialogInput(
            state, enter, nullptr, nullptr);
        expect(accepted.requestSave && accepted.closeDialog,
            "Enter accepts selected save slot");
        expect(accepted.playClick, "Enter keeps retail dialog click feedback");
    }

    void testPersistence()
    {
        using namespace monopoly;
        constexpr std::size_t slot = optionsui::SaveSlotCount - 1;
        Backup blobBackup(optionsui::gameBlobPath(slot));
        Backup metadataBackup(optionsui::gameMetadataPath(slot));

        optionsui::SaveRuntimeState save{};
        save.dialog = optionsui::FileDialogMode::Save;
        save.selectedSlot = static_cast<int>(slot);
        save.draftDescription = u"Runtime save test";

        rules::GameState rules{};
        rules.squares[0].gameEarnings = -1;
        rules.squares[1].gameEarnings = 12345;
        rules.squares[2].gameEarnings =
            static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) + 42;

        const auto prepared = optionsui::beginPendingSave(
            save, rules, 7, 11, {});
        expect(static_cast<bool>(prepared), "pending save metadata prepares");

        const std::vector<std::uint8_t> blob{
            'M','O','N','O','P','O','L','Y',1,2,3,4,5,6,7,8};
        const auto persisted = optionsui::persistPendingSave(save, blob);
        expect(static_cast<bool>(persisted), "pending save persists blob and sidecar");
        expect(std::filesystem::file_size(optionsui::gameMetadataPath(slot)) == 476,
            "legacy .sgd sidecar keeps 476-byte SaveGameStruct layout");

        optionsui::SaveRuntimeState load{};
        const auto refreshed = optionsui::refreshSaveSlots(
            load, optionsui::FileDialogMode::Load);
        expect(static_cast<bool>(refreshed), "saved slots refresh from disk");
        expect(load.slots[slot].occupied, "persisted slot is discovered");
        expect(load.slots[slot].metadata.description == u"Runtime save test",
            "sidecar description round-trips");
        expect(load.slots[slot].metadata.city == 7 &&
            load.slots[slot].metadata.system == 11,
            "sidecar city and monetary system round-trip");
        expect(load.slots[slot].metadata.squareGameEarnings[0] == 0 &&
            load.slots[slot].metadata.squareGameEarnings[1] == 12345 &&
            load.slots[slot].metadata.squareGameEarnings[2] ==
                std::numeric_limits<std::uint32_t>::max(),
            "legacy earnings conversion clamps to unsigned long");

        load.selectedSlot = static_cast<int>(slot);
        const auto restoredBlob = optionsui::readSelectedGameBlob(load);
        expect(restoredBlob && *restoredBlob == blob,
            "selected .msv blob round-trips exactly");

        rules::GameState restored{};
        int city = 0;
        int system = 0;
        const auto applied = optionsui::applySelectedMetadata(
            load, restored, city, system);
        expect(static_cast<bool>(applied), "selected metadata applies");
        expect(city == 7 && system == 11,
            "loaded metadata restores display city and system");
        expect(restored.squares[1].gameEarnings == 12345,
            "loaded metadata restores square earnings");
    }

    void testGeometry()
    {
        using namespace monopoly::optionsui;
        const auto first = saveSlotRect(0);
        const auto last = saveSlotRect(SaveSlotCount - 1);
        expect(first.left == 152 && first.top == 112 &&
            first.right == 648 && first.bottom == 146,
            "first save-slot hit box matches retail GameFileNameRects");
        expect(last.top == 384 && last.bottom == 418,
            "save-slot hit boxes retain retail 68-pixel cadence");
    }
}

int main()
{
    testGeometry();
    testInput();
    testPersistence();

    if (failures != 0)
    {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }

    std::cout << "Options save runtime tests passed\n";
    return 0;
}
