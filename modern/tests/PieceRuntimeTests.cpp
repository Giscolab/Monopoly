#include "PieceRuntime.hpp"
#include "LegacyDataArchiveBuilder.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    using namespace monopoly;
    using namespace monopoly::data;
    using namespace monopoly::sequence;
    int failures{};

    void expect(bool value, std::string_view text)
    {
        std::cout << (value ? "[PASS] " : "[FAIL] ") << text << '\n';
        if (!value) ++failures;
    }
    bool near(float a, float b) { return std::fabs(a - b) < 0.0001F; }
    void word(DataBytes& bytes, std::uint32_t value)
    {
        for (unsigned shift = 0; shift != 32; shift += 8)
            bytes.push_back(static_cast<std::byte>((value >> shift) & 255U));
    }
    DataBytes chunk(std::uint8_t id, const DataBytes& payload)
    {
        DataBytes result;
        word(result, (static_cast<std::uint32_t>(id) << 24U) |
            static_cast<std::uint32_t>(payload.size() + 4));
        result.insert(result.end(), payload.begin(), payload.end());
        return result;
    }
    DataBytes mesh(DataId target)
    {
        DataBytes payload;
        word(payload, 0);
        word(payload, 4U << 24U);
        word(payload, 18U); // Hold + absolute DATA references.
        word(payload, target);
        return chunk(9, payload);
    }

    struct Fixture
    {
        std::filesystem::path root;
        Fixture()
        {
            root = std::filesystem::current_path() /
                ("PieceRuntime-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
            if (!std::filesystem::create_directory(root))
                throw std::runtime_error("piece runtime fixture collision");
        }
        ~Fixture()
        {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }
    };

    std::shared_ptr<const SequenceProgram> program(Fixture& fixture)
    {
        const std::array items{ArchiveBuildItem{LegacyDataType::Chunky,
            mesh(packDataId(77, 9))}};
        const auto path = fixture.root / "token-runtime.dat";
        expect(writeLegacyDataArchive(path, items).has_value(),
            "synthetic token sequence archive is written");
        DataBankRegistry registry;
        expect(registry.mount(path, 2).has_value(),
            "synthetic token sequence archive mounts");
        const auto loaded = SequenceProgram::load(registry, packDataId(2, 0));
        expect(loaded.has_value(), "synthetic token mesh program loads");
        return loaded ? *loaded : nullptr;
    }

    Matrix3D poseMatrix(float yaw, float x, float y, float z)
    {
        auto matrix = moveRySTxzTransform(yaw, 1.0F, x, z);
        matrix.values[13] = y;
        return matrix;
    }

    bool samePose(const pieces::TokenPose& pose,
        float x, float y, float z, float yaw)
    {
        return near(pose.x, x) && near(pose.y, y) &&
            near(pose.z, z) && near(pose.yaw, yaw);
    }

    void testActualOrientationResolutionOrderAndCache()
    {
        Fixture fixture;
        auto executable = program(fixture);
        const auto id = packDataId(2, 0);
        SequenceRuntime runtime;
        pieces::TokenPoseTracker tracker;
        pieces::TokenRuntimeSources sources;

        expect(samePose(tracker.locate(0, 1, sources, runtime), 0, 0, 0, 0),
            "missing runtime data starts with the source LastKnownData zero pose");
        expect(samePose(tracker.locate(2, 1, sources, runtime), 0, 0, 0, 0),
            "invalid player always returns zeros");

        const auto idle = runtime.start(executable, pieces::TokenPriority).value();
        const auto moving = runtime.start(executable, pieces::Generic3DPriority).value();
        expect(runtime.update(0).has_value(), "idle and moving token sequences start");
        auto idleMatrix = poseMatrix(0.25F, 10.0F, 5.0F, 20.0F);
        auto movingMatrix = poseMatrix(0.75F, 30.0F, 7.0F, 40.0F);
        expect(runtime.moveMatching(id, pieces::TokenPriority,
            SequenceTransform(idleMatrix)) == 1 &&
            runtime.moveMatching(id, pieces::Generic3DPriority,
                SequenceTransform(movingMatrix)) == 1 && runtime.update(0).has_value(),
            "idle and moving token matrices are published in the same runtime");
        sources.idleSequences[0] = id;
        sources.movingSequence = id;
        auto pose = tracker.locate(0, 1, sources, runtime);
        expect(samePose(pose, 10.0F, 5.0F, 20.0F, 0.25F),
            "idle Player3DTokenShown branch wins before the global moving sequence");

        expect(runtime.stop(idle).has_value(), "idle sequence can disappear between display frames");
        pose = tracker.locate(0, 1, sources, runtime);
        expect(samePose(pose, 10.0F, 5.0F, 20.0F, 0.25F),
            "stale idle ID blocks lower else-if branches and returns LastKnownData exactly as source");
        sources.idleSequences[0].reset();
        pose = tracker.locate(0, 1, sources, runtime);
        expect(samePose(pose, 30.0F, 7.0F, 40.0F, 0.75F),
            "moving TokenCurrent3DSequence resolves through GetChildMeshWorldMatrix");

        sources.movingSequence.reset();
        expect(runtime.stop(moving).has_value(), "global moving sequence stops");
        const auto movingOut = runtime.start(executable, pieces::TokenPriority).value();
        expect(runtime.update(0).has_value() &&
            runtime.moveMatching(id, pieces::TokenPriority,
                SequenceTransform(poseMatrix(1.0F, 50.0F, 9.0F, 60.0F))) == 1 &&
            runtime.update(0).has_value(), "moving-out sequence publishes its matrix");
        sources.playerMovingOut = rules::PlayerNumber{0};
        sources.playerMovingOutSequence = id;
        pose = tracker.locate(0, 1, sources, runtime);
        expect(samePose(pose, 50.0F, 9.0F, 60.0F, 1.0F),
            "PlayerMovingOut branch uses token priority and child mesh matrix");

        expect(runtime.stop(movingOut).has_value(), "moving-out sequence stops");
        sources.playerMovingOut.reset();
        sources.playerMovingOutSequence.reset();
        const auto movingIn = runtime.start(executable, pieces::TokenPriority).value();
        expect(runtime.update(0).has_value() &&
            runtime.moveMatching(id, pieces::TokenPriority,
                SequenceTransform(poseMatrix(-0.5F, 70.0F, 11.0F, 80.0F))) == 1 &&
            runtime.update(0).has_value(), "moving-in sequence publishes its matrix");
        sources.playerMovingIn = rules::PlayerNumber{0};
        sources.playerMovingInSequence = id;
        pose = tracker.locate(0, 1, sources, runtime);
        expect(samePose(pose, 70.0F, 11.0F, 80.0F, -0.5F),
            "PlayerMovingIn branch follows moving-out with identical source priority");

        expect(runtime.stop(movingIn).has_value(), "moving-in sequence stops");
        sources.playerMovingIn.reset();
        sources.playerMovingInSequence.reset();
        const auto jail = runtime.start(executable, pieces::JailAnimationPriority).value();
        expect(runtime.update(0).has_value() &&
            runtime.moveMatching(id, pieces::JailAnimationPriority,
                SequenceTransform(poseMatrix(-1.0F, 90.0F, 13.0F, 100.0F))) == 1 &&
            runtime.update(0).has_value(), "jail animation sequence publishes its matrix");
        sources.jailTokenAnimation = id;
        pose = tracker.locate(0, 1, sources, runtime);
        expect(samePose(pose, 90.0F, 13.0F, 100.0F, -1.0F),
            "go-to-jail TokenAnimation uses historical fixed priority 77");
        expect(runtime.stop(jail).has_value(), "jail animation stops");
        pose = tracker.locate(0, 1, sources, runtime);
        expect(samePose(pose, 90.0F, 13.0F, 100.0F, -1.0F),
            "failed active branch returns the most recent LastKnownData pose");
        pieces::TokenPoseTracker freshTracker;
        expect(samePose(freshTracker.locate(0, 1, {}, runtime), 0, 0, 0, 0),
            "a fresh tracker starts with the legacy static cache's zero initialization");
    }
}

int main()
{
    testActualOrientationResolutionOrderAndCache();
    std::cout << (failures ? "Piece runtime tests FAILED\n" : "Piece runtime tests passed\n");
    return failures ? 1 : 0;
}
