#include "ModernTokenCatalog.hpp"
#include "PieceRuntime.hpp"

#include <iostream>
#include <string_view>

namespace
{
    int failures{};
    void expect(bool condition, std::string_view message)
    {
        if (condition) std::cout << "[PASS] " << message << '\n';
        else { std::cerr << "[FAIL] " << message << '\n'; ++failures; }
    }
}

int main()
{
    using namespace monopoly;
    using namespace monopoly::data;
    constexpr std::uint8_t Hat = 3;
    const auto hatMesh = representativeLegacyMesh(Hat);
    const auto hatIdle = idleSequenceDataId(Hat);
    const auto hatOneSquare = hatIdle + 1;
    expect(hatMesh == packDataId(LegacyGroupId::ThreeD, 0x008D) &&
        hatOneSquare == packDataId(LegacyGroupId::ThreeD, 0x0237),
        "qualified root and mesh match the reviewed retail manifest IDs");
    expect(qualifiedModernTokenSequence(hatMesh, hatOneSquare, pieces::Generic3DPriority),
        "complete top-hat one-square sequence qualifies at production movement priority");
    expect(!qualifiedModernTokenSequence(hatMesh, hatOneSquare, pieces::Generic3DPriority - 1) &&
        !qualifiedModernTokenSequence(hatMesh, hatOneSquare, pieces::Generic3DPriority + 1),
        "movement qualification rejects adjacent priorities");
    expect(!qualifiedModernTokenSequence(hatMesh, hatOneSquare, pieces::TokenPriority),
        "movement root cannot enter through idle priority");
    expect(qualifiedModernTokenSequence(hatMesh, hatOneSquare + 1, pieces::Generic3DPriority),
        "reviewed complete top-hat two-square root qualifies");
    expect(!qualifiedModernTokenSequence(hatMesh, hatIdle + 14, pieces::Generic3DPriority),
        "unreviewed resting-transition root retains retail geometry");
    expect(!qualifiedModernTokenSequence(hatMesh, hatIdle + 7, pieces::Generic3DPriority),
        "multi-HMD top-hat mx7 root retains complete retail sequence");
    expect(!qualifiedModernTokenSequence(hatMesh + 1, hatOneSquare, pieces::Generic3DPriority),
        "alternate top-hat HMD cannot enter qualified movement root");
    expect(!qualifiedModernTokenSequence(representativeLegacyMesh(1), hatOneSquare, pieces::Generic3DPriority),
        "different token mesh cannot enter qualified top-hat root");
    expect(!qualifiedModernTokenSequence(hatMesh, std::nullopt, pieces::Generic3DPriority) &&
        !qualifiedModernTokenSequence(EmptyDataId, hatOneSquare, pieces::Generic3DPriority),
        "missing root or invalid mesh retains retail geometry");
    expect(!qualifiedModernTokenSequence(packDataId(LegacyGroupId::Board, dataTag(hatMesh)),
        hatOneSquare, pieces::Generic3DPriority), "foreign data group cannot qualify");

    struct ReviewedRoot { std::uint8_t token; DataTag tag; };
    constexpr ReviewedRoot reviewedRoots[]{
        {0, 0x010D}, {1, 0x0170}, {3, 0x021F}, {3, 0x0241},
        {4, 0x0282}, {4, 0x02A3}, {6, 0x0364}, {7, 0x03C2},
        {8, 0x042F}, {9, 0x0471}, {9, 0x0489}, {9, 0x0493}, {10, 0x04EB}};
    for (const auto& reviewed : reviewedRoots)
    {
        const auto root = packDataId(LegacyGroupId::ThreeD, reviewed.tag);
        const auto mesh = representativeLegacyMesh(reviewed.token);
        expect(qualifiedModernTokenSequence(mesh, root, pieces::Generic3DPriority),
            "explicit reviewed root qualifies only its representative modern token mesh");
        expect(!qualifiedModernTokenSequence(mesh + 1, root, pieces::Generic3DPriority),
            "reviewed root never qualifies alternate HMD geometry");
        expect(!qualifiedModernTokenSequence(mesh, root, pieces::Generic3DPriority + 1),
            "reviewed root preserves nonmovement priority fallback");
    }
    expect(qualifiedModernTokenSequence(hatMesh,
        packDataId(LegacyGroupId::ThreeD, 0x023F), pieces::Generic3DPriority),
        "reviewed complete root with CNK-owned disappearance still qualifies");
    expect(!qualifiedModernTokenSequence(representativeLegacyMesh(0),
        packDataId(LegacyGroupId::ThreeD, 0x00F6), pieces::Generic3DPriority),
        "multi-HMD cannon corner retains retail geometry");
    expect(!qualifiedModernTokenSequence(hatMesh,
        packDataId(LegacyGroupId::Board, 0x0237), pieces::Generic3DPriority),
        "matching movement tag in a foreign root data group cannot qualify");

    for (const auto& definition : modernTokenDefinitions())
    {
        const auto mesh = representativeLegacyMesh(definition.token);
        const auto idle = idleSequenceDataId(definition.token);
        const bool eligible = definition.staticIdleReplacement;
        expect(qualifiedModernTokenSequence(mesh, idle, pieces::TokenPriority) == eligible &&
            qualifiedModernTokenSequence(mesh, idle,
                static_cast<std::uint16_t>(pieces::TokenPriority + rules::MaxPlayers - 1)) == eligible,
            "existing idle eligibility preserved at both player-priority boundaries");
        expect(!qualifiedModernTokenSequence(mesh, idle, pieces::TokenPriority - 1) &&
            !qualifiedModernTokenSequence(mesh, idle,
                static_cast<std::uint16_t>(pieces::TokenPriority + rules::MaxPlayers)),
            "idle qualification rejects both excluded priority boundaries");
    }
    expect(qualifiedModernTokenSequence(hatMesh + 1, hatIdle, pieces::TokenPriority),
        "existing alternate-HMD idle eligibility remains unchanged");
    expect(!qualifiedModernTokenSequence(representativeLegacyMesh(2), idleSequenceDataId(2),
        pieces::TokenPriority), "shape-changing dog idle retains retail mesh fallback");
    expect(!qualifiedModernTokenSequence(representativeLegacyMesh(2), idleSequenceDataId(2) + 1,
        pieces::Generic3DPriority), "unqualified dog movement retains retail mesh fallback");
    expect(!qualifiedModernTokenSequence(representativeLegacyMesh(5), idleSequenceDataId(5) + 1,
        pieces::Generic3DPriority), "shape-changing horse movement retains retail mesh fallback");
    return failures ? 1 : 0;
}
