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
            std::filesystem::create_directories(root / "tokens/horse_variants");
        }
        ~Fixture() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
        std::filesystem::path path(std::size_t state) const
        {
            if (kind == ModernTokenVariantKind::ShipMovement)
                return root / shipMovementVariantDefinitions()[state].relativeGlbPath;
            if (kind == ModernTokenVariantKind::DogIdle)
                return root / dogIdleVariantDefinitions()[state].relativeGlbPath;
            return root / horseIdleVariantDefinitions()[state].relativeGlbPath;
        }
        void write(std::size_t state, float minimumY, bool reversed = false,
            std::filesystem::path relativePath = {})
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
            const auto outputPath = relativePath.empty() ? path(state) : root / relativePath;
            std::filesystem::create_directories(outputPath.parent_path());
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!output) throw std::runtime_error("cannot create token variant fixture");
        }
    };

    void testReviewedProfileExtension()
    {
        using namespace monopoly;
        constexpr DataId Bag = 0x00080006, BagRoot = 0x000804D9;
        constexpr DataId Iron = 0x000800AD, IronBent = 0x000800AE;
        constexpr DataId IronSingle = 0x00080282, IronTriple = 0x00080283, IronFuture = 0x00080284;
        constexpr DataId Horse = 0x00080094, HorseNew = 0x00080097;
        constexpr DataId HorsePair = 0x000802E5, HorseSingle = 0x000802EA, HorseIdle = 0x000802FC;
        constexpr auto Priority = pieces::Generic3DPriority;
        Fixture fixture;
        fixture.write(0, 0.F, false, "tokens/moneybag_variants/pose_0006.glb");
        fixture.write(0, 0.F, false, "tokens/iron_variants/pose_00ad.glb");
        fixture.write(0, .02F, false, "tokens/iron_variants/pose_00ae.glb");
        ModernTokenVariantCache incomplete(fixture.root);
        const auto bag = incomplete.resolve(Bag, BagRoot, Priority);
        const auto iron = incomplete.resolve(Iron, IronSingle, Priority);
        expect(bag && std::abs(bag->bounds.minimum[1] + 4.F) < .001F &&
            iron && std::abs(iron->bounds.minimum[1]) < .001F,
            "reviewed shared frames retain moneybag restY minus4 and iron restY zero without runtime regrounding");
        expect(!incomplete.resolve(Iron, IronTriple, Priority) && incomplete.loadError(IronTriple) &&
            bag == incomplete.resolve(Bag, BagRoot, Priority) && iron == incomplete.resolve(Iron, IronSingle, Priority),
            "missing later iron state rejects its entire triple while disjoint iron and moneybag roots remain modern");
        fixture.write(0, .03F, false, "tokens/iron_variants/pose_00af.glb");
        expect(!incomplete.resolve(Iron, IronTriple, Priority), "new profile failure remains memoized after asset repair");
        ModernTokenVariantCache complete(fixture.root);
        const auto sharedIron = complete.resolve(Iron, IronTriple, Priority);
        const auto bent = complete.resolve(IronBent, IronTriple, Priority);
        expect(sharedIron && bent && sharedIron == complete.resolve(Iron, IronSingle, Priority) &&
            complete.rejectPack(bent.get()) && !complete.resolve(Iron, IronTriple, Priority) &&
            !complete.resolve(Iron, IronFuture, Priority) && sharedIron == complete.resolve(Iron, IronSingle, Priority) &&
            complete.resolve(Bag, BagRoot, Priority),
            "shared iron rejection blocks published and future referring roots while healthy subsets and moneybag remain available");
        expect(qualifiedModernTokenVariantSequence(Bag, BagRoot, Priority) &&
            !qualifiedModernTokenVariantSequence(Bag, BagRoot, pieces::TokenPriority) &&
            !qualifiedModernTokenVariantSequence(Bag, BagRoot, 0) &&
            !qualifiedModernTokenVariantSequence(0x00080008, BagRoot, Priority),
            "new complete profiles retain exact movement context and reject folded unreviewed moneybag states");
        fixture.kind = ModernTokenVariantKind::HorseIdle;
        fixture.write(0, 0.F);
        ModernTokenVariantCache missingHorse(fixture.root);
        expect(!missingHorse.resolve(Horse, HorsePair, Priority) && missingHorse.loadError(HorsePair) &&
            missingHorse.resolve(Horse, HorseSingle, Priority),
            "missing new horse pose cannot publish a partial pair or taint canonical single-state geometry");
        fixture.write(0, .02F, false, "tokens/horse_variants/pose_0097.glb");
        for (std::size_t state = 1; state < horseIdleVariantDefinitions().size(); ++state) fixture.write(state, 0.F);
        ModernTokenVariantCache horseCache(fixture.root);
        const auto canonical = horseCache.resolve(Horse, HorseIdle, pieces::TokenPriority);
        const auto newPose = horseCache.resolve(HorseNew, HorsePair, Priority);
        expect(canonical && std::abs(canonical->bounds.minimum[1] - 1.F) < .001F && newPose &&
            std::abs(newPose->bounds.minimum[1] - (1.F + .02F * 211.87215F)) < .001F &&
            canonical == horseCache.resolve(Horse, HorsePair, Priority),
            "horse extension reuses unchanged canonical idle geometry and preserves shared restY plus1");
        expect(newPose && horseCache.rejectPack(newPose.get()) && !horseCache.resolve(Horse, HorsePair, Priority) &&
            canonical == horseCache.resolve(Horse, HorseIdle, pieces::TokenPriority),
            "new horse pose rejection leaves the existing six-state idle pack modern");
    }

    void testMixedShipGroundingFormats()
    {
        using namespace monopoly;
        constexpr DataId Rest = 0x00080018, Squash = 0x0008001B, GroundedPose = 0x00080019;
        constexpr DataId CanonicalRoot = 0x00080360, MixedRoot = 0x00080356;
        Fixture fixture;
        fixture.write(0, .025F);
        fixture.write(1, .04F);
        fixture.write(0, .01F, false, "tokens/ship_variants/pose_0019.glb");
        for (const bool mixedFirst : {false, true})
        {
            ModernTokenVariantCache cache(fixture.root);
            const auto initial = cache.resolve(Rest, mixedFirst ? MixedRoot : CanonicalRoot, pieces::Generic3DPriority);
            const auto rest = cache.resolve(Rest, MixedRoot, pieces::Generic3DPriority);
            const auto squash = cache.resolve(Squash, CanonicalRoot, pieces::Generic3DPriority);
            const auto grounded = cache.resolve(GroundedPose, MixedRoot, pieces::Generic3DPriority);
            expect(initial && rest == initial && squash && grounded &&
                std::abs(rest->bounds.minimum[1]) < .001F &&
                std::abs(squash->bounds.minimum[1] - .015F * 87.55F) < .001F &&
                std::abs(grounded->bounds.minimum[1] - .01F * 87.55F) < .001F,
                "mixed ship root subtracts common baseline only from raw canonical states, preserving already-grounded pose height");
            expect(grounded && cache.rejectPack(grounded.get()) &&
                !cache.resolve(Rest, MixedRoot, pieces::Generic3DPriority) &&
                initial == cache.resolve(Rest, CanonicalRoot, pieces::Generic3DPriority),
                "grounded ship pose GPU rejection leaves disjoint canonical rest/squash root modern");
        }
    }

    void testCompleteRootSubsets()
    {
        using namespace monopoly;
        constexpr DataId Base = 0x00080032, Bent = 0x00080033, Later = 0x00080036;
        constexpr DataId BaseOnly = 0x00080160, BentOnly = 0x00080162;
        constexpr DataId Pair = 0x00080161, Triple = 0x00080159, Future = 0x0008015A;
        constexpr auto Priority = pieces::Generic3DPriority;
        std::size_t generic{}, idle{};
        bool excluded = true;
        for (const auto& root : modernTokenVariantRootDefinitions())
        {
            if (root.idlePriority) ++idle; else ++generic;
            for (const auto mesh : root.requiredMeshes)
                if (mesh == 0x000800CD) excluded = false;
        }
        expect(generic == 606 && idle == 2 && excluded,
            "explicit complete table retains 606 finished roots and two idle roots without excluded thimble pose");
        expect(qualifiedModernTokenVariantSequence(Base, Pair, Priority) &&
            qualifiedModernTokenVariantSequence(Bent, Pair, Priority) &&
            !qualifiedModernTokenVariantSequence(Later, Pair, Priority) &&
            !qualifiedModernTokenVariantSequence(Base, Pair, 0) &&
            !qualifiedModernTokenVariantSequence(Base, Pair, pieces::TokenPriority) &&
            !qualifiedModernTokenVariantSequence(Base, Pair, Priority - 1) &&
            !qualifiedModernTokenVariantSequence(Base, Pair, Priority + 1),
            "each complete generic root requires only its reviewed HMD subset and exact root priority100");
        Fixture fixture;
        fixture.write(0, 0.F, false, "tokens/race_car_variants/pose_0032.glb");
        ModernTokenVariantCache missing(fixture.root);
        expect(!missing.resolve(Base, Pair, Priority) && missing.loadError(Pair),
            "missing later required state cannot publish an earlier valid state of the root");
        const auto disjoint = missing.resolve(Base, BaseOnly, Priority);
        expect(disjoint && !missing.loadError(BaseOnly),
            "a failed pair leaves its healthy single-state subset available");
        fixture.write(0, .1F, false, "tokens/race_car_variants/pose_0033.glb");
        expect(!missing.resolve(Bent, Pair, Priority) && !missing.resolve(Bent, BentOnly, Priority),
            "shared missing geometry is remembered across repaired roots until cache owner reset");
        for (const bool pairFirst : {false, true})
        {
            ModernTokenVariantCache cache(fixture.root);
            const auto first = cache.resolve(Base, pairFirst ? Pair : BaseOnly, Priority);
            const auto second = cache.resolve(Base, pairFirst ? BaseOnly : Pair, Priority);
            const auto bent = cache.resolve(Bent, BentOnly, Priority);
            expect(first && first == second && bent && bent == cache.resolve(Bent, Pair, Priority),
                "shared immutable per-HMD pointers survive both single/pair cache activation orders");
            expect(!cache.resolve(Base, Triple, Priority) && cache.loadError(Triple) &&
                second == cache.resolve(Base, Pair, Priority),
                "unused token states are not preloaded and a missing third state only rejects its complete root");
            expect(bent && cache.rejectPack(bent.get()) && !cache.resolve(Base, Pair, Priority) &&
                !cache.resolve(Bent, BentOnly, Priority) && cache.loadError(Pair) && cache.loadError(BentOnly),
                "one shared GPU failure invalidates every already-published referring root");
            expect(!cache.attempted(Future) && !cache.resolve(Base, Future, Priority) && cache.loadError(Future) &&
                first == cache.resolve(Base, BaseOnly, Priority),
                "GPU-rejected geometry also blocks future referring roots while disjoint subset remains modern");
            expect(!cache.rejectPack(bent.get()), "repeated GPU rejection preserves remembered immutable identity");
        }
        fixture.write(0, .2F, true, "tokens/race_car_variants/pose_0036.glb");
        ModernTokenVariantCache topology(fixture.root);
        expect(!topology.resolve(Base, Triple, Priority) && topology.loadError(Triple) &&
            topology.resolve(Base, Pair, Priority),
            "complete-root topology mismatch rejects that subset without disabling a valid disjoint pack");
    }

    void testHorseIdleCompletePack()
    {
        using namespace monopoly;
        constexpr auto HorseRoot = packDataId(LegacyGroupId::ThreeD, 0x02FC);
        constexpr auto DogRoot = packDataId(LegacyGroupId::ThreeD, 0x01D3);
        constexpr auto ShipRoot = packDataId(LegacyGroupId::ThreeD, 0x0360);
        const auto priority = pieces::TokenPriority;
        const auto last = static_cast<std::uint16_t>(priority + rules::MaxPlayers - 1);
        for (const auto& state : horseIdleVariantDefinitions())
        {
            expect(qualifiedModernTokenVariantSequence(state.legacyMeshId, HorseRoot, priority) &&
                qualifiedModernTokenVariantSequence(state.legacyMeshId, HorseRoot, last),
                "every reviewed horse idle HMD qualifies at both player-priority boundaries");
            expect(!qualifiedModernTokenVariantSequence(state.legacyMeshId, HorseRoot, priority - 1) &&
                !qualifiedModernTokenVariantSequence(state.legacyMeshId, HorseRoot, last + 1) &&
                !qualifiedModernTokenVariantSequence(state.legacyMeshId, HorseRoot, 0) &&
                !qualifiedModernTokenVariantSequence(state.legacyMeshId, HorseRoot, pieces::Generic3DPriority) &&
                !qualifiedModernTokenVariantSequence(state.legacyMeshId, HorseRoot + 1, priority) &&
                !qualifiedModernTokenVariantSequence(state.legacyMeshId, std::nullopt, priority) &&
                !qualifiedModernTokenVariantSequence(state.legacyMeshId, DogRoot, priority),
                "horse idle requires its exact root and idle context independently of leaf priority");
        }
        expect(!qualifiedModernTokenVariantSequence(packDataId(LegacyGroupId::ThreeD, 0x0095), HorseRoot, priority) &&
            !qualifiedModernTokenVariantSequence(packDataId(LegacyGroupId::Board, 0x0094), HorseRoot, priority),
            "unreviewed horse poses and matching tags from another group retain retail");
        Fixture fixture(ModernTokenVariantKind::HorseIdle);
        for (std::size_t state = 0; state < horseIdleVariantDefinitions().size(); ++state)
        {
            ModernTokenVariantCache missing(fixture.root);
            expect(!missing.resolve(horseIdleVariantDefinitions()[0].legacyMeshId, HorseRoot, priority) &&
                missing.loadError(HorseRoot) && !missing.attempted(DogRoot) && !missing.attempted(ShipRoot),
                "any missing horse state prevents publication of the entire six-state pack");
            fixture.write(state, static_cast<float>(state) * .02F - .03F);
            expect(!missing.resolve(horseIdleVariantDefinitions()[state].legacyMeshId, HorseRoot, last),
                "horse load failure is remembered after file repair and across idle priorities");
        }
        fixture.kind = ModernTokenVariantKind::DogIdle;
        for (std::size_t state = 0; state < dogIdleVariantDefinitions().size(); ++state) fixture.write(state, 0.F);
        fixture.kind = ModernTokenVariantKind::ShipMovement;
        fixture.write(0, 2.F); fixture.write(1, 3.F);
        fixture.kind = ModernTokenVariantKind::HorseIdle;
        ModernTokenVariantCache complete(fixture.root);
        const auto dog = complete.resolve(dogIdleVariantDefinitions()[0].legacyMeshId, DogRoot, priority);
        const auto ship = complete.resolve(shipMovementVariantDefinitions()[0].legacyMeshId, ShipRoot, pieces::Generic3DPriority);
        std::array<std::shared_ptr<const MeshRenderData>, 6> poses;
        for (std::size_t state = 0; state < poses.size(); ++state)
        {
            poses[state] = complete.resolve(horseIdleVariantDefinitions()[state].legacyMeshId, HorseRoot, priority);
            const auto expectedY = (static_cast<float>(state) * .02F - .03F) * 211.87215F + 1.F;
            expect(poses[state] && std::abs(poses[state]->bounds.minimum[1] - expectedY) < .001F,
                "horse poses preserve shared exported grounding with calibrated Y offset");
            expect(poses[state] && poses[state] == complete.resolve(horseIdleVariantDefinitions()[state].legacyMeshId,
                HorseRoot, last), "horse immutable state is reused across player contexts");
        }
        expect(poses[0] && std::abs(poses[0]->bounds.minimum[0] + 1.F) < .001F &&
            std::abs(poses[0]->bounds.maximum[2] - (211.87215F - 4.8420224136F)) < .001F,
            "horse pack uses frozen negative90 yaw, units and shared offsets");
        expect(dog && ship && poses[5] && complete.rejectPack(poses[5].get()) && complete.loadError(HorseRoot) &&
            !complete.loadError(DogRoot) && !complete.loadError(ShipRoot) &&
            dog == complete.resolve(dogIdleVariantDefinitions()[0].legacyMeshId, DogRoot, priority) &&
            ship == complete.resolve(shipMovementVariantDefinitions()[0].legacyMeshId, ShipRoot, pieces::Generic3DPriority),
            "one horse GPU failure invalidates only horse while healthy dog and ship remain cached");
        for (const auto& state : horseIdleVariantDefinitions())
            expect(!complete.resolve(state.legacyMeshId, HorseRoot, last), "rejected horse pack retains every state in retail");
        ModernTokenVariantCache independent(fixture.root);
        const auto healthyHorse = independent.resolve(horseIdleVariantDefinitions()[0].legacyMeshId, HorseRoot, priority);
        const auto rejectedDog = independent.resolve(dogIdleVariantDefinitions()[0].legacyMeshId, DogRoot, priority);
        expect(healthyHorse && rejectedDog && independent.rejectPack(rejectedDog.get()) && independent.loadError(DogRoot) &&
            !independent.loadError(HorseRoot) && healthyHorse == independent.resolve(
                horseIdleVariantDefinitions()[0].legacyMeshId, HorseRoot, last),
            "dog GPU rejection cannot taint the independently loaded healthy horse pack");
        {
            std::ofstream malformed(fixture.path(5), std::ios::binary | std::ios::trunc);
            malformed << "invalid horse GLB";
        }
        ModernTokenVariantCache invalid(fixture.root);
        expect(!invalid.resolve(horseIdleVariantDefinitions()[0].legacyMeshId, HorseRoot, priority) &&
            invalid.loadError(HorseRoot) && invalid.resolve(dogIdleVariantDefinitions()[0].legacyMeshId, DogRoot, priority) &&
            !invalid.loadError(DogRoot), "malformed final horse state cannot publish early poses or taint healthy dog");
        fixture.write(5, .07F, true);
        ModernTokenVariantCache topology(fixture.root);
        expect(!topology.resolve(horseIdleVariantDefinitions()[0].legacyMeshId, HorseRoot, priority) &&
            topology.loadError(HorseRoot), "one mismatched horse topology rejects the complete six-state pack");
    }

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
    testReviewedProfileExtension();
    testMixedShipGroundingFormats();
    testCompleteRootSubsets();
    testHorseIdleCompletePack();
    testDogIdleCompletePack();
    using namespace monopoly;
    constexpr auto Root = packDataId(LegacyGroupId::ThreeD, 0x0360);
    constexpr auto Rest = packDataId(LegacyGroupId::ThreeD, 0x0018);
    constexpr auto Squash = packDataId(LegacyGroupId::ThreeD, 0x001B);
    constexpr auto Priority = pieces::Generic3DPriority;
    constexpr DataId UnreviewedRoot = 0x0008FFFF;
    expect(qualifiedModernTokenVariantSequence(Rest, Root, Priority) &&
        qualifiedModernTokenVariantSequence(Squash, Root, Priority), "both reviewed ship states qualify");
    expect(!qualifiedModernTokenVariantSequence(Rest, UnreviewedRoot, Priority) &&
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
    expect(!missing.resolve(Rest, UnreviewedRoot, Priority) && !missing.attempted(),
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
    expect(rest && rest == valid.resolve(Rest, 0x00080348, Priority) &&
        squash && squash == valid.resolve(Squash, 0x00080349, Priority),
        "existing canonical ship states are shared across complete single and multi-state roots");
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
        !valid.resolve(Squash, Root, Priority) && !valid.resolve(Rest, 0x00080348, Priority) &&
        !valid.resolve(Squash, 0x00080349, Priority) && !valid.resolve(Rest, 0x0008034D, Priority) && valid.loadError(),
        "shared ship rest rejection disables published and future dependent roots without file retries");
    return failures ? 1 : 0;
}
