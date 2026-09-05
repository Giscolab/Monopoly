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
        append(contents, sequence()); // attribute scan must stop here
        LegacyChunkReader reader(std::make_shared<const DataBytes>(sequence(contents)));
        auto record = readLegacySequenceRecord(reader);
        auto attributes = record ? readLegacySequenceAttributes(reader) :
            std::expected<LegacySequenceAttributes, SequenceError>(
                std::unexpected(record.error()));
        expect(attributes && attributes->values.size() == 2,
            "attribute parser stops before the first child sequence");
        const auto* dim = attributes ? std::get_if<SequenceDimensionalityAttribute>(
            &attributes->values[0]) : nullptr;
        const auto* decoded = attributes ?
            std::get_if<Sequence3DOriginScaleRotateOffsetAttribute>(&attributes->values[1]) : nullptr;
        expect(dim && dim->value == 3 && decoded && near(decoded->offsetX, 10.F) &&
            near(decoded->yaw, .3F) && near(decoded->scaleZ, 4.F),
            "packed dimensionality and 3D OSRT fields decode in source order");

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

    void testRecursiveRuntimeWorldTransform()
    {
        const auto unique = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const auto path = std::filesystem::current_path() / ("SequenceTransforms-" + unique + ".dat");
        DataBytes childContents;
        append(childContents, offset3D(5, 0, 0));
        DataBytes rootContents;
        append(rootContents, offset3D(10, 0, 0));
        append(rootContents, sequence(childContents));
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
    testMatrixConventionsAndFirstTransform();
    testTweekerModesAndErrors();
    testRecursiveRuntimeWorldTransform();
    testTweekerOrderingAndInterpolation();
    std::cout << (failures ? "Sequence transform tests FAILED\n" :
        "Sequence transform tests passed\n");
    return failures ? 1 : 0;
}
