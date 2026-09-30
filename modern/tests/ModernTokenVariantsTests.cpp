#include "ModernTokenVariants.hpp"
#include "PieceRuntime.hpp"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace monopoly::data;
    int failures{};
    void expect(bool condition, std::string_view message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) ++failures;
    }
    void word(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    { for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift)); }

    struct Fixture
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("monopoly-ship-variants-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        ModernTokenVariantKind kind;
        explicit Fixture(ModernTokenVariantKind selected = ModernTokenVariantKind::ShipMovement) : kind(selected)
        {
            std::filesystem::create_directories(root / "tokens/ship_variants");
            std::filesystem::create_directories(root / "tokens/dog_variants");
        }
        ~Fixture() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
        std::filesystem::path path(std::size_t state) const
        {
            return root / (kind == ModernTokenVariantKind::ShipMovement ?
                shipMovementVariantDefinitions()[state].relativeGlbPath : dogIdleVariantDefinitions()[state].relativeGlbPath);
        }
        void write(std::size_t state, float minimumY, bool reversed = false)
        {
            std::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2}]}],"buffers":[{"byteLength":84}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":12}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5125,"count":3,"type":"SCALAR"}]})";
            while (json.size() % 4) json.push_back(' ');
            std::vector<std::uint8_t> binary;
            for (const float value : {0.F, minimumY, 0.F, 1.F, minimumY, 0.F,
                0.F, minimumY + 1.F, 0.F, 0.F, 0.F, 1.F, 0.F, 0.F, 1.F, 0.F, 0.F, 1.F})
                word(binary, std::bit_cast<std::uint32_t>(value));
            const std::array<std::uint32_t, 3> indices = reversed ?
                std::array<std::uint32_t, 3>{0U, 2U, 1U} :
                std::array<std::uint32_t, 3>{0U, 1U, 2U};
            for (const auto index : indices) word(binary, index);
            std::vector<std::uint8_t> bytes;
            word(bytes, 0x46546C67); word(bytes, 2);
            word(bytes, static_cast<std::uint32_t>(28 + json.size() + binary.size()));
            word(bytes, static_cast<std::uint32_t>(json.size())); word(bytes, 0x4E4F534A);
            bytes.insert(bytes.end(), json.begin(), json.end());
            word(bytes, static_cast<std::uint32_t>(binary.size())); word(bytes, 0x004E4942);
            bytes.insert(bytes.end(), binary.begin(), binary.end());
            std::ofstream output(path(state), std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!output) throw std::runtime_error("cannot create ship variant fixture");
        }
    };

    void testDogIdleCompletePack()
    {
        using namespace monopoly;
        constexpr auto DogRoot = packDataId(LegacyGroupId::ThreeD, 0x01D3);
        constexpr auto ShipRoot = packDataId(LegacyGroupId::ThreeD, 0x0360);
        const auto firstPriority = pieces::TokenPriority;
        const auto lastPriority = static_cast<std::uint16_t>(pieces::TokenPriority + rules::MaxPlayers - 1);
        for (const auto& state : dogIdleVariantDefinitions())
        {
            expect(qualifiedModernTokenVariantSequence(state.legacyMeshId, DogRoot, firstPriority) &&
                qualifiedModernTokenVariantSequence(state.legacyMeshId, DogRoot, lastPriority),
                "all four complete dog idle states qualify at both production player-priority boundaries");
            expect(!qualifiedModernTokenVariantSequence(state.legacyMeshId, DogRoot, firstPriority - 1) &&
                !qualifiedModernTokenVariantSequence(state.legacyMeshId, DogRoot, lastPriority + 1) &&
                !qualifiedModernTokenVariantSequence(state.legacyMeshId, DogRoot, pieces::Generic3DPriority) &&
                !qualifiedModernTokenVariantSequence(state.legacyMeshId, DogRoot + 1, firstPriority),
                "dog pack rejects movement priority, excluded player bounds and unreviewed root");
        }
        Fixture fixture(ModernTokenVariantKind::DogIdle);
        for (std::size_t state = 0; state < dogIdleVariantDefinitions().size(); ++state)
        {
            ModernTokenVariantCache incomplete(fixture.root);
            expect(!incomplete.resolve(dogIdleVariantDefinitions()[0].legacyMeshId, DogRoot, firstPriority) &&
                incomplete.loadError(DogRoot) && !incomplete.attempted(ShipRoot),
                "any missing dog state retains all four states in retail without attempting ship pack");
            fixture.write(state, static_cast<float>(state) * .02F - .03F);
            expect(!incomplete.resolve(dogIdleVariantDefinitions()[state].legacyMeshId, DogRoot, firstPriority),
                "failed dog pack cannot switch into modern geometry after incremental file repair");
        }
        fixture.kind = ModernTokenVariantKind::ShipMovement;
        fixture.write(0, 2.F); fixture.write(1, 3.F);
        fixture.kind = ModernTokenVariantKind::DogIdle;
        ModernTokenVariantCache complete(fixture.root);
        const auto ship = complete.resolve(shipMovementVariantDefinitions()[0].legacyMeshId,
            ShipRoot, pieces::Generic3DPriority);
        expect(ship && complete.attempted(ShipRoot) && !complete.attempted(DogRoot),
            "healthy ship pack remains independently cached before loading dog pack");
        std::array<std::shared_ptr<const MeshRenderData>, 4> poses;
        for (std::size_t state = 0; state < poses.size(); ++state)
        {
            poses[state] = complete.resolve(dogIdleVariantDefinitions()[state].legacyMeshId, DogRoot, firstPriority);
            const float authoredMinimum = static_cast<float>(state) * .02F - .03F;
            expect(poses[state] && std::abs(poses[state]->bounds.minimum[1] - authoredMinimum * 154.80F) < .001F,
                "dog states preserve exported shared grounding and signed individual pose heights");
        }
        expect(complete.attempted(DogRoot) && !complete.loadError(DogRoot) &&
            !complete.loadError(ShipRoot) && poses[0] && poses[3] && poses[0] != poses[3],
            "all four dog states publish transactionally with distinct owned immutable identities");
        expect(poses[0] && std::abs(poses[0]->bounds.minimum[2] - (-154.80F - 15.05967734F)) < .001F,
            "dog pack uses the frozen positive90 yaw and shared calibrated Z offset");
        expect(poses[2] && complete.rejectPack(poses[2].get()) && complete.loadError(DogRoot) &&
            !complete.loadError(ShipRoot) && ship == complete.resolve(shipMovementVariantDefinitions()[0].legacyMeshId,
                ShipRoot, pieces::Generic3DPriority),
            "GPU rejection disables the complete dog pack while preserving healthy ship pack");
        for (const auto& state : dogIdleVariantDefinitions())
            expect(!complete.resolve(state.legacyMeshId, DogRoot, lastPriority),
                "one rejected dog pose forces retail for every pose and every idle player priority");
        fixture.kind = ModernTokenVariantKind::ShipMovement;
        std::filesystem::remove(fixture.path(1));
        fixture.kind = ModernTokenVariantKind::DogIdle;
        ModernTokenVariantCache missingShip(fixture.root);
        expect(!missingShip.resolve(shipMovementVariantDefinitions()[0].legacyMeshId, ShipRoot,
                pieces::Generic3DPriority) && missingShip.loadError(ShipRoot) &&
            missingShip.resolve(dogIdleVariantDefinitions()[0].legacyMeshId, DogRoot, firstPriority) &&
            !missingShip.loadError(DogRoot),
            "failed ship pack cannot taint loading or error reporting of the complete dog pack");
        {
            std::ofstream malformed(fixture.path(3), std::ios::binary | std::ios::trunc);
            malformed << "invalid dog GLB";
        }
        ModernTokenVariantCache invalid(fixture.root);
        expect(!invalid.resolve(dogIdleVariantDefinitions()[0].legacyMeshId, DogRoot, firstPriority) &&
            invalid.loadError(DogRoot) && !invalid.loadError(ShipRoot),
            "malformed final dog pose prevents publishing earlier valid dog poses");
        fixture.write(3, .03F, true);
        ModernTokenVariantCache topology(fixture.root);
        expect(!topology.resolve(dogIdleVariantDefinitions()[0].legacyMeshId, DogRoot, firstPriority) &&
            topology.loadError(DogRoot), "one mismatched dog pose topology rejects the whole four-state pack");
    }
}

int main()
{
    testDogIdleCompletePack();
    using namespace monopoly;
    constexpr auto Root = packDataId(LegacyGroupId::ThreeD, 0x0360);
    constexpr auto Rest = packDataId(LegacyGroupId::ThreeD, 0x0018);
    constexpr auto Squash = packDataId(LegacyGroupId::ThreeD, 0x001B);
    constexpr auto Priority = pieces::Generic3DPriority;
    expect(qualifiedModernTokenVariantSequence(Rest, Root, Priority) &&
        qualifiedModernTokenVariantSequence(Squash, Root, Priority), "both reviewed ship states qualify");
    expect(!qualifiedModernTokenVariantSequence(Rest, Root + 1, Priority) &&
        !qualifiedModernTokenVariantSequence(Rest, Root, Priority + 1) &&
        !qualifiedModernTokenVariantSequence(Rest, Root, pieces::TokenPriority) &&
        !qualifiedModernTokenVariantSequence(Rest, std::nullopt, Priority) &&
        !qualifiedModernTokenVariantSequence(Rest + 1, Root, Priority),
        "unreviewed roots, priorities, missing root and unrelated HMD retain retail");
    expect(!qualifiedModernTokenVariantSequence(packDataId(LegacyGroupId::Board, 0x0018), Root, Priority) &&
        !qualifiedModernTokenVariantSequence(Rest, packDataId(LegacyGroupId::Board, 0x0360), Priority),
        "foreign data groups cannot qualify by matching low tags");

    Fixture fixture;
    ModernTokenVariantCache missing(fixture.root);
    expect(!missing.resolve(Rest, Root + 1, Priority) && !missing.attempted(),
        "unqualified lookup does not attempt optional file loading");
    expect(!missing.resolve(Rest, Root, Priority) && missing.attempted() && missing.loadError(),
        "missing base state rejects and remembers the whole pack");
    fixture.write(0, 2.F);
    ModernTokenVariantCache missingSquash(fixture.root);
    expect(!missingSquash.resolve(Rest, Root, Priority) && !missingSquash.resolve(Squash, Root, Priority),
        "valid base cannot publish when the alternate state is missing");
    {
        std::ofstream invalid(fixture.path(1), std::ios::binary);
        invalid << "invalid GLB";
    }
    ModernTokenVariantCache malformed(fixture.root);
    expect(!malformed.resolve(Rest, Root, Priority) && malformed.loadError() &&
        !malformed.resolve(Squash, Root, Priority), "malformed alternate state retains whole-root retail fallback");
    fixture.write(1, 3.F);
    expect(!malformed.resolve(Rest, Root, Priority) && !missingSquash.resolve(Rest, Root, Priority),
        "failed pack remains cached after files change, avoiding mid-sequence replacement");
    fixture.write(1, 3.F, true);
    ModernTokenVariantCache mismatched(fixture.root);
    expect(!mismatched.resolve(Rest, Root, Priority) && mismatched.loadError() &&
        !mismatched.resolve(Squash, Root, Priority), "different triangle correspondence rejects the whole pack");
    fixture.write(1, 3.F);

    ModernTokenVariantCache valid(fixture.root);
    auto rest = valid.resolve(Rest, Root, Priority);
    auto squash = valid.resolve(Squash, Root, Priority);
    expect(rest && squash && rest != squash && !valid.loadError(),
        "complete valid pack selects distinct owned immutable states by actual HMD");
    expect(rest && rest == valid.resolve(Rest, Root, Priority) &&
        squash && squash == valid.resolve(Squash, Root, Priority), "successful pack is reused without reloading");
    if (rest && squash)
    {
        expect(std::abs(rest->bounds.minimum[1]) < .001F &&
            std::abs(squash->bounds.minimum[1] - 87.55F) < .001F,
            "both states share rest grounding; alternate ground movement remains intact");
        expect(rest->indices == squash->indices && rest->vertices.size() == squash->vertices.size(),
            "validated states preserve authored vertex and triangle correspondence");
    }
    expect(!valid.resolve(Rest + 1, Root, Priority) && !valid.resolve(Rest, Root, Priority - 1),
        "loaded pack still enforces root, HMD and production priority context");
    MeshRenderData unrelated;
    expect(!valid.rejectPack(&unrelated) && rest == valid.resolve(Rest, Root, Priority),
        "unrelated GPU rejection does not invalidate a healthy pack");
    expect(rest && valid.rejectPack(rest.get()) && !valid.resolve(Rest, Root, Priority) &&
        !valid.resolve(Squash, Root, Priority) && valid.loadError(),
        "either state upload failure invalidates both states without file retries");
    return failures ? 1 : 0;
}
