#include "SequenceRuntime.hpp"
#include "LegacyDataArchiveBuilder.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>

namespace
{
    using namespace monopoly::data;
    using namespace monopoly::sequence;
    int failures{};
    void expect(bool condition, std::string_view text)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << text << '\n';
        if (!condition) ++failures;
    }
    bool near(float left, float right)
    { return std::fabs(left - right) < 0.0001F; }
    void word(DataBytes& bytes, std::uint32_t value)
    {
        for (unsigned shift = 0; shift != 32; shift += 8)
            bytes.push_back(static_cast<std::byte>((value >> shift) & 255U));
    }
    void real(DataBytes& bytes, float value) { word(bytes, std::bit_cast<std::uint32_t>(value)); }
    void append(DataBytes& target, const DataBytes& source)
    { target.insert(target.end(), source.begin(), source.end()); }
    DataBytes chunk(std::uint8_t id, const DataBytes& payload)
    {
        DataBytes result;
        word(result, (static_cast<std::uint32_t>(id) << 24U) |
            static_cast<std::uint32_t>(payload.size() + 4));
        append(result, payload);
        return result;
    }
    DataBytes sequence(const DataBytes& contents = {}, bool dropFrames = false)
    {
        DataBytes payload;
        word(payload, 0);
        word(payload, (4U << 24U) |
            (dropFrames ? 0x4000'0000U : 0U) | 20U);
        word(payload, 2U);
        append(payload, contents);
        return chunk(1, payload);
    }
    DataBytes tweeker(std::int32_t start, std::int32_t end,
        std::uint8_t interpolation, std::uint8_t endingAction,
        const DataBytes& attributes = {})
    {
        DataBytes payload;
        word(payload, static_cast<std::uint32_t>(start) & 0x00FF'FFFFU);
        word(payload, (4U << 24U) | 0x4000'0000U |
            (static_cast<std::uint32_t>(end) & 0x00FF'FFFFU));
        word(payload, endingAction);
        payload.push_back(static_cast<std::byte>(interpolation));
        append(payload, attributes);
        return chunk(10, payload);
    }
    DataBytes offset3D(float x, float y, float z)
    { DataBytes bytes; real(bytes, x); real(bytes, y); real(bytes, z); return chunk(133, bytes); }

    void testImmutableAttributeDecode()
    {
        DataBytes contents;
        append(contents, chunk(129, DataBytes{std::byte{3}}));
        DataBytes osrt;
        for (float value : std::array{10.F, 20.F, 30.F, 1.F, 2.F, 3.F,
            .1F, .2F, .3F, 2.F, 3.F, 4.F}) real(osrt, value);
        append(contents, chunk(135, osrt));
        append(contents, chunk(141, DataBytes{
            std::byte{0x22}, std::byte{0x56}})); // 22050 Hz.
        append(contents, chunk(142, DataBytes{std::byte{70}}));
        append(contents, chunk(143, DataBytes{std::byte{231}})); // -25.
        append(contents, sequence()); // only dimensionality inference stops here
        LegacyChunkReader reader(std::make_shared<const DataBytes>(sequence(contents)));
        auto record = readLegacySequenceRecord(reader);
        auto attributes = record ? readLegacySequenceAttributes(reader) :
            std::expected<LegacySequenceAttributes, SequenceError>(
                std::unexpected(record.error()));
        expect(attributes && attributes->values.size() == 5 && attributes->firstChildAttributeIndex == 5,
            "attribute parser records the first-child dimensionality boundary");
        const auto* dim = attributes ? std::get_if<SequenceDimensionalityAttribute>(
            &attributes->values[0]) : nullptr;
        const auto* decoded = attributes ?
            std::get_if<Sequence3DOriginScaleRotateOffsetAttribute>(&attributes->values[1]) : nullptr;
        expect(dim && dim->value == 3 && decoded && near(decoded->offsetX, 10.F) &&
            near(decoded->yaw, .3F) && near(decoded->scaleZ, 4.F),
            "packed dimensionality and 3D OSRT fields decode in source order");
        const auto* pitch = attributes ?
            std::get_if<SequenceSoundPitchAttribute>(&attributes->values[2]) : nullptr;
        const auto* volume = attributes ?
            std::get_if<SequenceSoundVolumeAttribute>(&attributes->values[3]) : nullptr;
        const auto* pan = attributes ?
            std::get_if<SequenceSoundPanningAttribute>(&attributes->values[4]) : nullptr;
        expect(pitch && pitch->pitch == 22050 &&
            volume && volume->volume == 70 &&
            pan && pan->panning == -25,
            "packed sequence pitch volume and panning attributes decode exactly");

        DataBytes shortOffset(11, std::byte{});
        LegacyChunkReader truncated(std::make_shared<const DataBytes>(
            sequence(chunk(133, shortOffset))));
        (void)readLegacySequenceRecord(truncated);
        const auto bad = readLegacySequenceAttributes(truncated);
        expect(!bad && bad.error().code == SequenceErrorCode::AttributeTruncated,
            "truncated transform data fails before reading native structures");
        LegacyChunkReader invalid(std::make_shared<const DataBytes>(
            sequence(chunk(129, DataBytes{std::byte{1}}))));
        (void)readLegacySequenceRecord(invalid);
        const auto badDim = readLegacySequenceAttributes(invalid);
        expect(!badDim && badDim.error().code == SequenceErrorCode::InvalidDimensionality,
            "historically invalid one-dimensional attribute is rejected");
        LegacyChunkReader bounded(std::make_shared<const DataBytes>(sequence(contents)));
        (void)readLegacySequenceRecord(bounded);
        const auto limited = readLegacySequenceAttributes(bounded, 1);
        expect(!limited && limited.error().code == SequenceErrorCode::AttributeLimitExceeded,
            "attribute decoding has an explicit anti-amplification limit");
    }

    void testBoundsInferenceAndChildBoundary()
    {
        DataBytes box2D;
        for (const auto value : {1U, 2U, 30U, 40U}) word(box2D, value);
        DataBytes sphere; real(sphere, 12.5F);
        DataBytes choice(4, std::byte{}); real(choice, 0.5F);
        const std::array hints{
            chunk(136, box2D), chunk(137, DataBytes(96, std::byte{})),
            chunk(138, sphere), chunk(139, choice)};
        for (std::size_t index = 0; index < hints.size(); ++index)
        {
            LegacyChunkReader reader(std::make_shared<const DataBytes>(sequence(hints[index])));
            const auto record = readLegacySequenceRecord(reader);
            const auto attributes = readLegacySequenceAttributes(reader);
            expect(record && attributes, "bounding and mesh-choice hint bytes decode");
            if (!record || !attributes) continue;
            const auto initial = initialSequenceTransform(*record, *attributes, 0);
            const auto expectedDimension = index == 0 ? 2 : 3;
            const auto* two = std::get_if<Matrix2D>(&initial.local);
            const auto* three = std::get_if<Matrix3D>(&initial.local);
            expect(initial.dimensionality == expectedDimension && !initial.explicitlyPositioned &&
                (index == 0 ? two && two->values == identity2D().values :
                    three && three->values == identity3D().values),
                "bounds and mesh choice infer dimension without acting as positioning matrices");
        }

        // The first sphere settles dimensionality. A later explicit 2D marker
        // cannot override it, but a compatible position after a child is used.
        DataBytes contents = hints[2];
        append(contents, sequence(offset3D(777, 0, 0)));
        append(contents, chunk(129, DataBytes{std::byte{2}}));
        append(contents, offset3D(9, 8, 7));
        LegacyChunkReader reader(std::make_shared<const DataBytes>(sequence(contents)));
        const auto record = readLegacySequenceRecord(reader);
        const auto attributes = readLegacySequenceAttributes(reader);
        expect(record && attributes && attributes->firstChildAttributeIndex == 1 && attributes->values.size() == 3,
            "sphere hint and late positioning survive decoding without adopting child attributes");
        if (record && attributes)
        {
            const auto initial = initialSequenceTransform(*record, *attributes, 2);
            const auto* matrix = std::get_if<Matrix3D>(&initial.local);
            expect(initial.dimensionality == 3 && initial.explicitlyPositioned && matrix &&
                near(matrix->values[12], 9) && near(matrix->values[13], 8) && near(matrix->values[14], 7),
                "sphere hint wins over later dimensionality while positioning scans past the child");
        }

        DataBytes childFirst = sequence(offset3D(777, 0, 0));
        append(childFirst, hints[2]);
        append(childFirst, chunk(129, DataBytes{std::byte{3}}));
        append(childFirst, offset3D(100, 200, 300));
        DataBytes offset2D; word(offset2D, 4); word(offset2D, 7);
        append(childFirst, chunk(130, offset2D));
        LegacyChunkReader inherited(std::make_shared<const DataBytes>(sequence(childFirst)));
        const auto inheritedRecord = readLegacySequenceRecord(inherited);
        const auto inheritedAttributes = readLegacySequenceAttributes(inherited);
        expect(inheritedRecord && inheritedAttributes && inheritedAttributes->firstChildAttributeIndex == 0,
            "child-first sequence retains an empty dimensionality prefix");
        if (inheritedRecord && inheritedAttributes)
        {
            const auto initial = initialSequenceTransform(*inheritedRecord, *inheritedAttributes, 2);
            const auto* matrix = std::get_if<Matrix2D>(&initial.local);
            expect(initial.dimensionality == 2 && initial.explicitlyPositioned && matrix &&
                near(matrix->values[6], 4) && near(matrix->values[7], 7),
                "late sphere and explicit 3D cannot override inherited 2D but late 2D position applies");
            const auto zero = initialSequenceTransform(*inheritedRecord, *inheritedAttributes, 0);
            expect(zero.dimensionality == 0 && !zero.explicitlyPositioned &&
                std::holds_alternative<std::monostate>(zero.local),
                "attributes after the first child cannot invent dimensions for a zero-dimensional parent");
        }
    }

    void testBoundsAreNotTransformKeys()
    {
        LegacySequenceAttributes keys;
        keys.values.push_back(Sequence2DBoundingBoxAttribute{{}, 1, 2, 3, 4});
        keys.values.push_back(Sequence3DBoundingBoxAttribute{});
        keys.values.push_back(Sequence3DBoundingSphereAttribute{{}, 42.0F});
        keys.values.push_back(Sequence3DMeshChoiceAttribute{{}, 0, 1, 0.5F});
        const auto boundsOnly = evaluateTweekerTransform(keys, 2, 5, 10, 3);
        expect(boundsOnly && !boundsOnly->changed,
            "bounding boxes, sphere and mesh choice never become transformation tweeker keys");
        keys.values.push_back(Sequence3DOffsetAttribute{{}, 7, 8, 9});
        const auto selectedBounds = evaluateTweekerTransform(keys, 2, 5, 10, 3);
        expect(selectedBounds && !selectedBounds->changed,
            "first private bounds attribute prevents falling through to a later transform type");
        keys.values.clear();
        keys.values.push_back(SequenceDimensionalityAttribute{{}, 3});
        keys.values.push_back(SequenceFileNameAttribute{{}, 1, "unused.wav"});
        keys.values.push_back(Sequence3DOffsetAttribute{{}, 7, 8, 9});
        keys.values.push_back(SequenceSoundVolumeAttribute{{}, 20});
        keys.values.push_back(Sequence3DOffsetAttribute{{}, 17, 18, 19});
        keys.values.push_back(Sequence3DOffsetAttribute{{}, 100, 100, 100});
        const auto interpolated = evaluateTweekerTransform(keys, 2, 5, 10, 3);
        const auto* matrix = interpolated ? std::get_if<Matrix3D>(&interpolated->transform) : nullptr;
        expect(interpolated && interpolated->changed && matrix && near(matrix->values[12], 12) &&
            near(matrix->values[13], 13) && near(matrix->values[14], 14),
            "first transform type uses its first two keys across unrelated attributes only");
    }

    void testMatrixConventionsAndFirstTransform()
    {
        LegacySequenceRecord record{};
        record.data = SequenceGroupingData{};
        LegacySequenceAttributes attributes;
        attributes.values.push_back(Sequence3DOriginScaleRotateOffsetAttribute{
            {}, 10, 20, 30, 1, 2, 3, 0, 0, 0, 2, 3, 4});
        auto initial = initialSequenceTransform(record, attributes, 0);
        const auto& matrix = std::get<Matrix3D>(initial.local).values;
        expect(initial.dimensionality == 3 && initial.explicitlyPositioned &&
            near(matrix[0], 2) && near(matrix[5], 3) && near(matrix[10], 4) &&
            near(matrix[12], 8) && near(matrix[13], 14) && near(matrix[14], 18),
            "3D OSRT applies negative origin, scale, roll/pitch/yaw, then offset");

        LegacySequenceAttributes firstOnly;
        firstOnly.values.push_back(Sequence2DOffsetAttribute{{}, 4, 7});
        firstOnly.values.push_back(Sequence2DOffsetAttribute{{}, 400, 700});
        const auto first = initialSequenceTransform(record, firstOnly, 0);
        const auto& firstMatrix = std::get<Matrix2D>(first.local).values;
        expect(first.dimensionality == 2 && near(firstMatrix[6], 4) && near(firstMatrix[7], 7),
            "only the first applicable transform subchunk positions a sequence");

        constexpr float halfPi = 1.57079632679489661923F;
        const auto xysr = moveXYSRTransform(10, 20, 2.0F, halfPi);
        expect(near(xysr.values[0], 0.0F) &&
            near(xysr.values[1], 2.0F) &&
            near(xysr.values[3], -2.0F) &&
            near(xysr.values[4], 0.0F) &&
            near(xysr.values[6], 10.0F) &&
            near(xysr.values[7], 20.0F),
            "StartCXYSR applies rotate, uniform scale, then translation in row-vector order");

        const auto world = composeSequenceWorld(first.local, 2,
            SequenceTransform(translate2D(10, 20)), 2);
        const auto& worldMatrix = std::get<Matrix2D>(world).values;
        expect(near(worldMatrix[6], 14) && near(worldMatrix[7], 27),
            "row-vector composition applies local transform before parent world");

        LegacySequenceAttributes explicitZero;
        explicitZero.values.push_back(SequenceDimensionalityAttribute{{}, 0});
        explicitZero.values.push_back(Sequence3DOffsetAttribute{{}, 1, 2, 3});
        const auto zero = initialSequenceTransform(record, explicitZero, 2);
        expect(zero.dimensionality == 0 && std::holds_alternative<std::monostate>(zero.local),
            "first explicit dimensionality wins and mismatched later transforms are ignored");
    }

    void testTweekerModesAndErrors()
    {
        LegacySequenceAttributes keys;
        keys.values.clear();
        keys.values.push_back(Sequence3DOffsetAttribute{{}, 7, 8, 9});
        keys.values.push_back(Sequence3DOffsetAttribute{{}, 17, 18, 19});

        const auto constant = evaluateTweekerTransform(keys, 1, 5, 10, 3);
        const auto* constantMatrix = constant ?
            std::get_if<Matrix3D>(&constant->transform) : nullptr;
        expect(constant && constant->changed && !constant->identity &&
            constantMatrix && near(constantMatrix->values[12], 7),
            "constant tweeker keeps its first transform key");

        const auto infiniteLinear = evaluateTweekerTransform(
            keys, 2, 5, SequenceClock::InfiniteEndTime, 3);
        const auto* infiniteMatrix = infiniteLinear ?
            std::get_if<Matrix3D>(&infiniteLinear->transform) : nullptr;
        expect(infiniteLinear && infiniteMatrix && near(infiniteMatrix->values[12], 7),
            "infinite linear tweeker follows the historical constant-key path");

        const auto invalid = evaluateTweekerTransform(keys, 3, 5, 10, 3);
        expect(!invalid && invalid.error() == TweekerTransformError::InvalidInterpolation,
            "unimplemented tweeker interpolation fails explicitly");
        const auto mismatch = evaluateTweekerTransform(keys, 2, 5, 10, 2);
        expect(!mismatch && mismatch.error() == TweekerTransformError::DimensionalityMismatch,
            "tweeker key dimensionality must match its parent");
    }

    void testRecursiveRuntimeWorldTransform(bool latePosition)
    {
        const auto unique = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const auto path = std::filesystem::current_path() / ("SequenceTransforms-" + unique + ".dat");
        DataBytes childContents;
        append(childContents, offset3D(5, 0, 0));
        DataBytes rootContents;
        if (latePosition)
            append(rootContents, chunk(129, DataBytes{std::byte{3}}));
        else
            append(rootContents, offset3D(10, 0, 0));
        append(rootContents, sequence(childContents));
        if (latePosition)
            append(rootContents, offset3D(10, 0, 0));
        const std::array items{ArchiveBuildItem{LegacyDataType::Chunky, sequence(rootContents)}};
        expect(writeLegacyDataArchive(path, items).has_value(), "synthetic transformed DAT is written");
        DataBankRegistry registry;
        expect(registry.mount(path, 2).has_value(), "synthetic transformed DAT mounts");
        auto program = SequenceProgram::load(registry, packDataId(2, 0));
        expect(program.has_value(), "program accepts implemented transform attributes");
        SequenceRuntime runtime;
        auto root = program ? runtime.start(*program, 1) :
            std::expected<SequenceNodeId, RuntimeError>(std::unexpected(RuntimeError{}));
        expect(root && runtime.update(0).has_value(), "transformed recursive runtime updates");
        const auto rootView = root ? runtime.inspect(*root) : std::nullopt;
        const auto childView = rootView && !rootView->children.empty() ?
            runtime.inspect(rootView->children.front()) : std::nullopt;
        const auto* childWorld = childView ? std::get_if<Matrix3D>(&childView->worldTransform) : nullptr;
        expect(rootView && rootView->dimensionality == 3 && childWorld &&
            !rootView->tweekerTransformApplied && near(childWorld->values[12], 15),
            latePosition ? "runtime child inherits parent positioning decoded after the child record" :
                "recursive runtime composes child local transform with parent world");
        registry.clear();
        expect(runtime.update(1).has_value() && childView,
            "decoded transforms remain owned after the registry snapshot is replaced");
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }

    void testTweekerOrderingAndInterpolation()
    {
        const auto unique = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const auto path = std::filesystem::current_path() / ("SequenceTweeker-" + unique + ".dat");
        DataBytes keys;
        append(keys, offset3D(0, 0, 0));
        append(keys, offset3D(20, 0, 0));
        DataBytes rootContents;
        append(rootContents, offset3D(10, 0, 0));
        append(rootContents, tweeker(0, 10, 2, 1, keys));
        DataBytes normalChild;
        append(normalChild, offset3D(5, 0, 0));
        append(rootContents, sequence(normalChild));
        append(rootContents, tweeker(10, 0, 0, 2)); // identity reset
        // This test isolates tweeker ordering. Let the root consume the entire
        // parent-clock jump so its local clock is exactly 5, then 10.
        const std::array items{ArchiveBuildItem{LegacyDataType::Chunky,
            sequence(rootContents, true)}};
        expect(writeLegacyDataArchive(path, items).has_value(), "synthetic tweeker DAT is written");
        DataBankRegistry registry;
        expect(registry.mount(path, 2).has_value(), "synthetic tweeker DAT mounts");
        auto program = SequenceProgram::load(registry, packDataId(2, 0));
        expect(program.has_value(), "tweeker records and transform keys form an immutable program");
        SequenceRuntime runtime;
        auto root = program ? runtime.start(*program, 1) :
            std::expected<SequenceNodeId, RuntimeError>(std::unexpected(RuntimeError{}));
        expect(root && runtime.update(0).has_value(), "initial tweeker update succeeds");
        expect(runtime.update(5).has_value(), "linear tweeker advances before parent position");
        const auto view = root ? runtime.inspect(*root) : std::nullopt;
        const auto* world = view ? std::get_if<Matrix3D>(&view->worldTransform) : nullptr;
        std::optional<SequenceNodeView> normal;
        if (view)
            for (const auto child : view->children)
                if (const auto candidate = runtime.inspect(child); candidate && candidate->dimensionality == 3)
                    normal = candidate;
        const auto* childWorld = normal ? std::get_if<Matrix3D>(&normal->worldTransform) : nullptr;
        expect(view && view->tweekerTransformApplied,
            "linear tweeker marks the parent transform as active");
        expect(world && near(world->values[12], 20),
            "linear tweeker is evaluated before the parent's local transform");
        expect(childWorld && near(childWorld->values[12], 25),
            "normal child sees the tweeked parent world in the same update");
        expect(runtime.update(10).has_value(), "identity tweeker boundary update succeeds");
        const auto reset = root ? runtime.inspect(*root) : std::nullopt;
        const auto* resetWorld = reset ? std::get_if<Matrix3D>(&reset->worldTransform) : nullptr;
        expect(reset && !reset->tweekerTransformApplied,
            "identity tweeker clears the retained parent tweeker flag at its start tick");
        expect(resetWorld && near(resetWorld->values[12], 10),
            "identity tweeker restores the parent's untweeked world transform");
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
}

int main()
{
    testImmutableAttributeDecode();
    testBoundsInferenceAndChildBoundary();
    testBoundsAreNotTransformKeys();
    testMatrixConventionsAndFirstTransform();
    testTweekerModesAndErrors();
    testRecursiveRuntimeWorldTransform(false);
    testRecursiveRuntimeWorldTransform(true);
    testTweekerOrderingAndInterpolation();
    std::cout << (failures ? "Sequence transform tests FAILED\n" :
        "Sequence transform tests passed\n");
    return failures ? 1 : 0;
}
