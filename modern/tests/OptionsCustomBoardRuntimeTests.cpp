#include "OptionsCustomBoardRuntime.hpp"
#include "SyntheticSavedCustomBoard.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
    int failures{};

    void expect(bool condition, std::string_view message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) ++failures;
    }

    struct TempTree final
    {
        std::filesystem::path root{
            std::filesystem::temp_directory_path() /
            "monopoly_options_custom_board_runtime_tests"};

        TempTree()
        {
            std::error_code error;
            std::filesystem::remove_all(root, error);
            std::filesystem::create_directories(root / "custbrds");
        }
        ~TempTree()
        {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }

        std::filesystem::path boards() const
        {
            return root / "custbrds";
        }
    };

    void writeBoard(const std::filesystem::path& directory,
        std::string_view name, std::uint32_t version,
        bool createAssets = true)
    {
        std::filesystem::create_directories(directory);
        std::ofstream output(directory / name,
            std::ios::binary | std::ios::trunc);
        const std::array<unsigned char, 4> bytes{
            static_cast<unsigned char>(version & 0xFFU),
            static_cast<unsigned char>((version >> 8U) & 0xFFU),
            static_cast<unsigned char>((version >> 16U) & 0xFFU),
            static_cast<unsigned char>((version >> 24U) & 0xFFU)};
        output.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (createAssets)
            std::filesystem::create_directories(
                directory / std::filesystem::path(name).stem());
    }

    monopoly::uimsg::Message click(int x, int y)
    {
        monopoly::uimsg::Message message{};
        message.type = monopoly::uimsg::Type::MouseLeftDown;
        message.numberA = x;
        message.numberB = y;
        return message;
    }

    void testGeometry()
    {
        using namespace monopoly::optionsui;
        const auto first = customBoardSlotRect(0);
        const auto last = customBoardSlotRect(CustomBoardPageSize - 1);
        expect(first.left == 152 && first.top == 112 &&
            first.right == 648 && first.bottom == 146,
            "first custom-board slot matches retail geometry");
        expect(last.left == 152 && last.top == 384 &&
            last.right == 648 && last.bottom == 418,
            "fifth custom-board slot keeps the retail 68-pixel cadence");
        const auto invalid = customBoardSlotRect(CustomBoardPageSize);
        expect(invalid.left == 0 && invalid.top == 0 &&
            invalid.right == 0 && invalid.bottom == 0,
            "out-of-range custom-board slot has no hit box");
    }
    void testEnumerationAndPaging()
    {
        using namespace monopoly;
        TempTree tree;
        constexpr std::uint32_t version = 0x78563412U;
        for (int index = 0; index < 12; ++index)
        {
            const auto name = "Board" +
                std::string(index < 10 ? "0" : "") +
                std::to_string(index) + ".brd";
            writeBoard(tree.boards(), name, version);
        }
        std::ofstream(tree.boards() / "Ignore.txt") << "not a board";

        optionsui::CustomBoardState state{};
        const auto opened = optionsui::openCustomBoardDialog(
            state, tree.root, display::Screen2D::PlayerSelect);
        expect(opened.has_value() && state.active,
            "custom-board dialog opens against executable-relative custbrds");
        expect(state.entries.size() == 12 && state.selectedIndex == 0 &&
            state.pageOffset == 0,
            "dialog enumerates only the twelve .brd files and selects first");
        expect(state.entries.front().displayName == "Board00.brd" &&
            state.entries.back().displayName == "Board11.brd",
            "custom boards are deterministically sorted by filename");
        expect(state.previousView == display::Screen2D::PlayerSelect,
            "dialog retains the caller backdrop for cancel/accept return");
        state.buttonRects = {{
            {10, 10, 30, 30},
            {40, 10, 60, 30},
            {70, 10, 90, 30},
            {100, 10, 120, 30}
        }};

        auto result = optionsui::processCustomBoardInput(
            state, click(105, 15));
        expect(result.consumed && result.playClick &&
            state.pageOffset == 5 && state.selectedIndex == 5,
            "Next advances exactly five boards and selects first visible row");
        expect(optionsui::customBoardHasPrevious(state) &&
            optionsui::customBoardHasNext(state),
            "middle page exposes both Previous and Next");

        result = optionsui::processCustomBoardInput(state, click(105, 15));
        expect(result.playClick && state.pageOffset == 10 &&
            state.selectedIndex == 10 &&
            !optionsui::customBoardHasNext(state),
            "last partial page starts at item ten and disables Next");

        result = optionsui::processCustomBoardInput(state, click(105, 15));
        expect(result.consumed && !result.playClick &&
            state.pageOffset == 10,
            "disabled Next is consumed without changing page or click sound");

        result = optionsui::processCustomBoardInput(state, click(75, 15));
        expect(result.playClick && state.pageOffset == 5 &&
            state.selectedIndex == 5,
            "Previous returns exactly one five-board page");

        const auto slot = optionsui::customBoardSlotRect(2);
        result = optionsui::processCustomBoardInput(
            state, click(slot.left + 1, slot.top + 1));
        expect(result.consumed && !result.playClick &&
            state.selectedIndex == 7,
            "clicking a board row selects its absolute board index");

        result = optionsui::processCustomBoardInput(state, click(15, 15));
        expect(result.playClick && result.requestLoad && !result.closeDialog,
            "Okay requests loading the selected custom board");

        result = optionsui::processCustomBoardInput(state, click(45, 15));
        expect(result.playClick && result.closeDialog && !result.requestLoad,
            "Cancel requests closing the custom-board dialog");

        optionsui::closeCustomBoardDialog(state);
        expect(!state.active &&
            std::all_of(state.buttonRects.begin(), state.buttonRects.end(),
                [](const optionsui::Rect& rect)
                {
                    return rect.left == 0 && rect.top == 0 &&
                        rect.right == 0 && rect.bottom == 0;
                }),
            "closing dialog disables input rectangles");
    }
    void testValidation()
    {
        using namespace monopoly;
        TempTree tree;
        constexpr std::uint32_t version = 0xA1B2C3D4U;
        writeBoard(tree.boards(), "Owned.brd", version);

        const auto accepted = optionsui::validateCustomBoardFile(
            tree.boards(), "Owned.brd", version);
        expect(accepted.has_value(),
            "matching legacy ownership DWORD accepts custom board");
        if (accepted)
        {
            expect(accepted->boardFile.filename() == "Owned.brd" &&
                accepted->assetRoot.filename() == "Owned",
                "validated board resolves sibling asset directory by stem");
            expect(accepted->boardFile.is_absolute() &&
                accepted->assetRoot.is_absolute(),
                "validated paths are canonical absolute paths");
        }

        expect(!optionsui::validateCustomBoardFile(
            tree.boards(), "Owned.brd", version ^ 1U),
            "mismatched installed editor security DWORD rejects board");
        expect(!optionsui::validateCustomBoardFile(
            tree.boards(), "../Owned.brd", version),
            "parent-path board names are rejected before filesystem access");
        expect(!optionsui::validateCustomBoardFile(
            tree.boards(), "Owned.txt", version),
            "non-.brd selections are rejected");
        writeBoard(tree.boards(), "NoAssets.brd", version, false);
        expect(!optionsui::validateCustomBoardFile(
            tree.boards(), "NoAssets.brd", version),
            "board without its sibling asset directory is rejected");

        {
            std::ofstream shortFile(tree.boards() / "Short.brd",
                std::ios::binary | std::ios::trunc);
            shortFile.put('x');
        }
        std::filesystem::create_directories(tree.boards() / "Short");
        expect(!optionsui::validateCustomBoardFile(
            tree.boards(), "Short.brd", version),
            "board file shorter than the legacy four-byte ownership code is rejected");
    }

    void testMissingDirectory()
    {
        using namespace monopoly;
        TempTree tree;
        std::filesystem::remove_all(tree.boards());

        optionsui::CustomBoardState state{};
        const auto opened = optionsui::openCustomBoardDialog(
            state, tree.root, display::Screen2D::PlayerSelect);
        expect(opened.has_value() && state.active && state.entries.empty() &&
            state.selectedIndex == -1,
            "missing custbrds directory opens an empty retail-compatible dialog");
    }
    void testSavedAssetRestoration()
    {
        using namespace monopoly;
        for (const bool europe : {false, true})
        {
            SyntheticSavedCustomBoard fixture(europe);
            const int currency = europe ? 5 : 13;
            const auto restored = optionsui::restoreSavedCustomBoard(
                fixture.savedName(), *fixture.resources, currency);
            expect(restored && *restored && **restored == fixture.root,
                "saved custom assets restore for USA and Europe without a .brd or registry ownership check");
            const auto relative = optionsui::restoreSavedCustomBoard(
                "Saved board", *fixture.resources, currency);
            expect(relative && *relative && **relative == fixture.root,
                "relative saved paths resolve through the explicit retail resource roots");
            std::filesystem::remove(fixture.root / "2DBoards/2DVIEW39.BMP");
            expect(!optionsui::restoreSavedCustomBoard(fixture.savedName(), *fixture.resources, currency),
                "a missing later camera rejects a partial set instead of selecting a stock board");
            SyntheticSavedCustomBoard::write(fixture.root / "2DBoards/2DVIEW39.BMP",
                SyntheticSequenceResources::bitmap24());
            std::ofstream(fixture.root / "2DBoards/2DVIEW02.BMP", std::ios::trunc) << "invalid";
            expect(!optionsui::restoreSavedCustomBoard(fixture.savedName(), *fixture.resources, currency),
                "malformed camera bytes fail complete-set preflight");
            SyntheticSavedCustomBoard::write(fixture.root / "2DBoards/2DVIEW02.BMP",
                SyntheticSequenceResources::bitmap24());
            std::filesystem::remove(fixture.root / "Photos/CT01_128.BMP");
            expect(!optionsui::restoreSavedCustomBoard(fixture.savedName(), *fixture.resources, currency),
                "missing 3D texture rejects restoration before publication even with complete 2D cameras");
            std::filesystem::remove(fixture.root / "2DBoards/2DVIEW01.BMP");
            const auto removed = optionsui::restoreSavedCustomBoard(
                fixture.savedName(), *fixture.resources, currency);
            expect(removed && !*removed,
                "only absent first camera requests the source deleted-board fallback");
            expect(!optionsui::restoreSavedCustomBoard("../escape", *fixture.resources, currency),
                "relative saved path traversal is an error rather than a deleted-board fallback");
        }
    }
}

int main()
{
    std::cout << "Monopoly custom-board runtime tests\n"
              << "===================================\n";
    testGeometry();
    testEnumerationAndPaging();
    testValidation();
    testMissingDirectory();
    testSavedAssetRestoration();

    std::cout << "Custom-board runtime failures: "
              << failures << '\n';
    return failures == 0 ? 0 : 1;
}
